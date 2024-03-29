/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/
/*
 *   Volume management functions for Storage Daemon
 *
 *   Kern Sibbald, MM
 *
 *   Split from reserve.c October 2008
 *
 */

#include "bacula.h"
#include "stored.h"

const int dbglvl =  150;

static dlist *vol_list = NULL;
static brwlock_t vol_list_lock;
static dlist *read_vol_list = NULL;
static bthread_mutex_t read_vol_lock = BTHREAD_MUTEX_PRIORITY(PRIO_SD_READ_VOL_LIST);

/* Forward referenced functions */
static void free_vol_item(VOLRES *vol);
static VOLRES *new_vol_item(DCR *dcr, const char *VolumeName);
static void debug_list_volumes(const char *imsg);

/*
 * For append volumes the key is the VolumeName.
 */
static int name_compare(void *item1, void *item2)
{
   return strcmp(((VOLRES *)item1)->vol_name, ((VOLRES *)item2)->vol_name);
}

/*
 * For read volumes the key is JobId, VolumeName.
 */
static int read_compare(void *item1, void *item2)
{
   VOLRES *vol1 = (VOLRES *)item1;
   VOLRES *vol2 = (VOLRES *)item2;

   if (vol1->get_jobid() == vol2->get_jobid()) {
      return strcmp(vol1->vol_name, vol2->vol_name);
   }
   if (vol1->get_jobid() < vol2->get_jobid()) {
      return -1;
   }
   return 1;
}

bool is_vol_list_empty()
{
   return vol_list->empty();
}

int vol_list_lock_count = 0;

/*
 *  Initialized the main volume list. Note, we are using a recursive lock.
 */
void init_vol_list_lock()
{
   int errstat;
   if ((errstat=rwl_init(&vol_list_lock, PRIO_SD_VOL_LIST)) != 0) {
      berrno be;
      Emsg1(M_ABORT, 0, _("Unable to initialize volume list lock. ERR=%s\n"),
            be.bstrerror(errstat));
   }
}

void term_vol_list_lock()
{
   rwl_destroy(&vol_list_lock);
}

/*
 * This allows a given thread to recursively call to lock_volumes()
 */
void _lock_volumes(const char *file, int line)
{
   int errstat;
   vol_list_lock_count++;
   if ((errstat=rwl_writelock_p(&vol_list_lock, file, line)) != 0) {
      berrno be;
      Emsg2(M_ABORT, 0, "rwl_writelock failure. stat=%d: ERR=%s\n",
           errstat, be.bstrerror(errstat));
   }
}

void _unlock_volumes()
{
   int errstat;
   vol_list_lock_count--;
   if ((errstat=rwl_writeunlock(&vol_list_lock)) != 0) {
      berrno be;
      Emsg2(M_ABORT, 0, "rwl_writeunlock failure. stat=%d: ERR=%s\n",
           errstat, be.bstrerror(errstat));
   }
}

#define lock_read_volumes() lock_read_volumes_p(__FILE__, __LINE__)
static void lock_read_volumes_p(const char *file="**Unknown", int line=0)
{
   bthread_mutex_lock_p(&read_vol_lock, file, line);
}

static void unlock_read_volumes()
{
   bthread_mutex_unlock(&read_vol_lock);
}

/*
 * Add a volume to the read list.
 * Note, we use VOLRES because it simplifies the code
 *   even though, the only part of VOLRES that we need is
 *   the volume name.  The same volume may be in the list
 *   multiple times, but each one is distinguished by the
 *   JobId.  We use JobId, VolumeName as the key.
 * We can get called multiple times for the same volume because
 *   when parsing the bsr, the volume name appears multiple times.
 */
void add_read_volume(JCR *jcr, const char *VolumeName)
{
   VOLRES *nvol, *vol;

   nvol = new_vol_item(NULL, VolumeName);
   nvol->set_jobid(jcr->JobId);
   nvol->set_reading();
   lock_read_volumes();
   vol = (VOLRES *)read_vol_list->binary_insert(nvol, read_compare);
   if (vol != nvol) {
      free_vol_item(nvol);
      Dmsg2(dbglvl, "read_vol=%s JobId=%d already in list.\n", VolumeName, jcr->JobId);
   } else {
      Dmsg2(dbglvl, "add read_vol=%s JobId=%d\n", VolumeName, jcr->JobId);
   }
   unlock_read_volumes();
}

/*
 * Check if volume name is in the read list.
 */
bool is_read_volume(JCR *jcr, const char *VolumeName)
{
   VOLRES vol, *fvol;
   lock_read_volumes();
   vol.vol_name = bstrdup(VolumeName);
   fvol = (VOLRES *)read_vol_list->binary_search(&vol, name_compare);
   free(vol.vol_name);
   unlock_read_volumes();
   return fvol != NULL;
}

/*
 * Remove a given volume name from the read list.
 */
void remove_read_volume(JCR *jcr, const char *VolumeName)
{
   VOLRES vol, *fvol;
   lock_read_volumes();
   vol.vol_name = bstrdup(VolumeName);
   vol.set_jobid(jcr->JobId);
   fvol = (VOLRES *)read_vol_list->binary_search(&vol, read_compare);
   free(vol.vol_name);
   if (fvol) {
      Dmsg3(dbglvl, "remove_read_vol=%s JobId=%d found=%d\n", VolumeName, jcr->JobId, fvol!=NULL);
   }
   if (fvol) {
      read_vol_list->remove(fvol);
      free_vol_item(fvol);
   }
   unlock_read_volumes();
// pthread_cond_broadcast(&wait_next_vol);
}

/*
 * List Volumes -- this should be moved to status.c
 */
enum {
   debug_lock = true,
   debug_nolock = false
};

static void debug_list_volumes(const char *imsg)
{
   VOLRES *vol;
   POOL_MEM msg(PM_MESSAGE);

   if (debug_level < dbglvl) {
      return;
   }

   foreach_vol(vol) {
      if (vol->dev) {
         Mmsg(msg, "List %s: %s in_use=%d swap=%d slot=%d on %s device %s\n", imsg,
              vol->vol_name, vol->is_in_use(), vol->is_swapping(),
              vol->get_slot(),
              vol->dev->print_type(), vol->dev->print_name());
      } else {
         Mmsg(msg, "List %s: %s in_use=%d swap=%d slot=%d no dev\n", imsg, vol->vol_name,
              vol->is_in_use(), vol->is_swapping(), vol->get_slot());
      }
      Dmsg1(dbglvl, "%s", msg.c_str());
   }
   endeach_vol(vol);
}


/*
 * List Volumes -- this should be moved to status.c
 */
void list_volumes(void sendit(const char *msg, int len, void *sarg), void *arg)
{
   VOLRES *vol;
   POOL_MEM msg(PM_MESSAGE);
   int len;

   foreach_vol(vol) {
      DEVICE *dev = vol->dev;
      if (dev) {
         len = Mmsg(msg, "Reserved volume: %s on %s device %s\n", vol->vol_name,
                  dev->print_type(), dev->print_name());
         sendit(msg.c_str(), len, arg);
         len = Mmsg(msg, "    Reader=%d writers=%d reserves=%d volinuse=%d worm=%d\n",
            dev->can_read()?1:0, dev->num_writers, dev->num_reserved(),
            vol->is_in_use(), dev->is_worm());
         sendit(msg.c_str(), len, arg);
      } else {
         len = Mmsg(msg, "Volume %s no device. volinuse=%d\n", vol->vol_name,
            vol->is_in_use());
         sendit(msg.c_str(), len, arg);
      }
   }
   endeach_vol(vol);

   lock_read_volumes();
   foreach_dlist(vol, read_vol_list) {
      DEVICE *dev = vol->dev;
      if (dev) {
         len = Mmsg(msg, "Read volume: %s on %s device %s\n", vol->vol_name,
                  dev->print_type(), dev->print_name());
         sendit(msg.c_str(), len, arg);
         len = Mmsg(msg, "    Reader=%d writers=%d reserves=%d volinuse=%d JobId=%d\n",
            dev->can_read()?1:0, dev->num_writers, dev->num_reserved(),
            vol->is_in_use(), vol->get_jobid());
         sendit(msg.c_str(), len, arg);
      } else {
         len = Mmsg(msg, "Volume: %s no device. volinuse=%d\n", vol->vol_name,
            vol->is_in_use());
         sendit(msg.c_str(), len, arg);
      }
   }
   unlock_read_volumes();
}

/*
 * Create a Volume item to put in the Volume list
 *   Ensure that the device points to it.
 */
static VOLRES *new_vol_item(DCR *dcr, const char *VolumeName)
{
   VOLRES *vol;
   vol = (VOLRES *)malloc(sizeof(VOLRES));
   memset(vol, 0, sizeof(VOLRES));
   vol->vol_name = bstrdup(VolumeName);
   if (dcr) {
      vol->dev = dcr->dev;
      Dmsg4(dbglvl, "new Vol=%s slot=%d at %p dev=%s\n",
            VolumeName, vol->get_slot(), vol->vol_name, vol->dev->print_name());
   }
   vol->init_mutex();
   vol->inc_use_count();
   return vol;
}

static void free_vol_item(VOLRES *vol)
{
   DEVICE *dev = NULL;

   vol->dec_use_count();
   vol->vLock();
   if (vol->use_count() > 0) {
      vol->vUnlock();
      return;
   }
   vol->vUnlock();
   free(vol->vol_name);
   if (vol->dev) {
      dev = vol->dev;
   }
   vol->destroy_mutex();
   free(vol);
   if (dev) {
      dev->vol = NULL;
   }
}

/*
 * Put a new Volume entry in the Volume list. This
 *  effectively reserves the volume so that it will
 *  not be mounted again.
 *
 * If the device has any current volume associated with it,
 *  and it is a different Volume, and the device is not busy,
 *  we release the old Volume item and insert the new one.
 *
 * It is assumed that the device is free and locked so that
 *  we can change the device structure.
 *
 * Some details of the Volume list handling:
 *
 *  1. The Volume list entry is attached to the drive (rather than
 *       attached to a job as it was previously. I.e. the drive that "owns"
 *       the volume (in use, mounted)
 *       must point to the volume (still to be maintained in a list).
 *
 *  2. The Volume is entered in the list when a drive is reserved.
 *
 *  3. When a drive is in use, the device code must appropriately update the
 *       volume name as it changes.
  *      This code keeps the same list entry as long as the drive
 *       has any volume associated with it but the volume name in the list
 *       must be updated when the drive has a different volume mounted.
 *
 *  4. A job that has reserved a volume, can un-reserve the volume, and if the
 *      volume is not mounted, and not reserved, and not in use, it will be
 *      removed from the list.
 *
 *  5. If a job wants to reserve a drive with a different Volume from the one on
 *      the drive, it can re-use the drive for the new Volume.

 *  6. If a job wants a Volume that is in a different drive, it can either use the
 *      other drive or take the volume, only if the other drive is not in use or
 *      not reserved.
 *
 *  One nice aspect of this is that the reserve use count and the writer use count
 *  already exist and are correctly programmed and will need no changes -- use
 *  counts are always very tricky.
 *
 *  The old code had a concept of "reserving" a Volume, but was changed
 *  to reserving and using a drive.  A volume is must be attached to (owned by) a
 *  drive and can move from drive to drive or be unused given certain specific
 *  conditions of the drive.  The key is that the drive must "own" the Volume.
 *
 *  Return: VOLRES entry on success
 *          NULL volume busy on another drive
 *               jcr->errmsg has details
 */
VOLRES *reserve_volume(DCR *dcr, const char *VolumeName)
{
   VOLRES *vol, *nvol;
   DEVICE * volatile dev = dcr->dev;
   JCR *jcr = dcr->jcr;

   jcr->errmsg[0] = 0;
   if (job_canceled(dcr->jcr)) {
      Mmsg1(jcr->errmsg, _("Could not reserve volume \"%s\", because job canceled.\n"),
         dev->VolHdr.VolumeName);
      return NULL;
   }
   ASSERT2(dev != NULL, "No device in reserve_volume!");

   Dmsg2(dbglvl, "enter reserve_volume=%s drive=%s\n", VolumeName,
      dcr->dev->print_name());

   /* If acquiring to write, don't accept a Volume in read list */
   if (dcr->is_writing() && is_read_volume(dcr->jcr, VolumeName)) {
      Mmsg1(jcr->errmsg, _("Could not reserve volume \"%s\" for append, because it will be read.\n"),
         dev->VolHdr.VolumeName);
      return NULL;
   }

   /*
    * We lock the reservations system here to ensure
    *  when adding a new volume that no newly scheduled
    *  job can reserve it.
    */
   lock_volumes();
   debug_list_volumes("begin reserve_volume");
   /*
    * First, remove any old volume attached to this device as it
    *  is no longer used.
    */
   if (dev->vol) {
      vol = dev->vol;
      Dmsg4(dbglvl, "Vol attached=%s, newvol=%s volinuse=%d on %s\n",
         vol->vol_name, VolumeName, vol->is_in_use(), dev->print_name());
      /*
       * Make sure we don't remove the current volume we are inserting
       *  because it was probably inserted by another job, or it
       *  is not being used and is marked as not reserved.
       */
      if (strcmp(vol->vol_name, VolumeName) == 0) {
         Dmsg3(dbglvl, "set reserved vol=%s slot=%d dev=%s\n", VolumeName,
               vol->get_slot(), vol->dev->print_name());
         goto get_out;                  /* Volume already on this device */
      } else {
         /* Don't release a volume if it was reserved by someone other than us */
         if (vol->is_in_use() && !dcr->reserved_volume) {
            Dmsg5(dbglvl, "Set wait(). Cannot free vol=%s for %s (JobId=%ld). volinuse=%d on %s\n",
               vol->vol_name, VolumeName, vol->get_jobid(), vol->is_in_use(), dev->print_name());
            Mmsg3(dcr->jcr->errmsg, _("Cannot reserve Volume=%s because drive is busy with Volume=%s (JobId=%ld).\n"),
               VolumeName, vol->vol_name, vol->get_jobid());
            dev->set_wait();
            vol = NULL;                  /* vol in use */
            goto get_out;
         }
         Dmsg2(dbglvl, "reserve_vol free vol=%s at %p\n", vol->vol_name, vol->vol_name);
         /* If old Volume is still mounted, must unload it */
         if (strcmp(vol->vol_name, dev->VolHdr.VolumeName) == 0) {
            Dmsg2(50, "set_unload vol=%s slot=%d\n", vol->vol_name, vol->get_slot());
            dev->set_unload();          /* have to unload current volume */
         }
         free_volume(dev);              /* Release old volume entry */
         debug_list_volumes("reserve_vol free");
      }
   }

   /* Create a new Volume entry */
   nvol = new_vol_item(dcr, VolumeName);

   /*
    * Handle request for read volume for file
    *  device, for which we assume we can open multiple
    *  devices to read the Volume.
    *
    * Note: when doing multiple simultaneous reads
    *  of the same volume, the volume names are not
    *  inserted into the write volume list.
    */
   if (dcr->is_reading() && dev->is_file()) {
      nvol->set_jobid(dcr->jcr->JobId);
      nvol->set_reading();
      vol = nvol;
      dev->vol = vol;
      goto get_out;
   } else {
      vol = (VOLRES *)vol_list->binary_insert(nvol, name_compare);
   }

   /*
    * This part handles any write volumes or read volumes that
    *  cannot be simultaneously on multiple devices.
    */
   if (vol != nvol) {
      /*
       * At this point, a Volume with this name already is in the list,
       *   so we simply release our new Volume entry. Note, this should
       *   only happen if we are moving the volume from one drive to another.
       */
      Dmsg2(dbglvl, "Found vol=%s dev-same=%d\n", vol->vol_name, dev==vol->dev);
      Dmsg2(dbglvl, "reserve_vol free-tmp vol=%s at %p\n",
            vol->vol_name, vol->vol_name);
      /*
       * Clear dev pointer so that free_vol_item() doesn't
       *  take away our volume.
       */
      nvol->dev = NULL;                  /* don't zap dev entry */
      free_vol_item(nvol);

      if (vol->dev) {
         Dmsg2(dbglvl, "dev=%s vol->dev=%s\n", dev->print_name(), vol->dev->print_name());
      }

      /*
       * Check if we are trying to use the Volume on a different drive
       *  dev      is our device
       *  vol->dev is where the Volume we want is
       */
      if (dev != vol->dev) {
         /* Caller wants to switch Volume to another device */
         if (!vol->dev->is_busy() && !vol->is_swapping()) {
            int32_t slot;
            Dmsg3(dbglvl, "==== Swap vol=%s from dev=%s to %s\n",
               VolumeName, vol->dev->print_name(), dev->print_name());
            free_volume(dev);            /* free any volume attached to our drive */
            Dmsg3(50, "set_unload vol=%s slot=%d dev=%s\n", vol->vol_name,
               vol->get_slot(), dev->print_name());
            dev->set_unload();           /* Unload any volume that is on our drive */
            dcr->set_dev(vol->dev);      /* temp point to other dev */
            slot = get_autochanger_loaded_slot(dcr);  /* get slot on other drive */
            dcr->set_dev(dev);           /* restore dev */
            vol->set_slot(slot);         /* save slot */
            vol->dev->set_unload();      /* unload the other drive */
            vol->set_swapping();         /* swap from other drive */
            dev->swap_dev = vol->dev;    /* remember to get this vol */
            dev->set_load();             /* then reload on our drive */
            vol->dev->vol = NULL;        /* remove volume from other drive */
            vol->dev = dev;              /* point the Volume at our drive */
            dev->vol = vol;              /* point our drive at the Volume */
         } else {
            if (dev) {
               Jmsg8(jcr, M_WARNING, 0, "Need volume for %s from other drive, "
                  "but swap not possible. Status: reader=%d writers=%d "
                  "reserves=%d swap=%d vol=%s from dev=%s to %s\n",
                  dcr->is_writing()?"write":"read",
                  vol->dev->can_read(), vol->dev->num_writers,
                  vol->dev->num_reserved(), vol->is_swapping(),
                  VolumeName, vol->dev->print_name(), dev->print_name());
            }
            if (vol->is_swapping()) {
               DEVICE *swapdev = dev->swap_dev;
               if (vol && dev && swapdev) {
                  Mmsg3(jcr->errmsg, _("Volume %s is busy swapping from %s to %s\n"),
                     NPRT(vol->vol_name), dev->print_name(), swapdev->print_name());
               } else {
                  Mmsg1(jcr->errmsg, _("Volume %s is busy swapping.\n"),
                     NPRT(vol->vol_name));
               }
            } else if (vol->dev) {
               Mmsg2(jcr->errmsg, _("%s device %s is busy.\n"),
                  vol->dev->print_type(), vol->dev->print_name());
            } else {
               Mmsg1(jcr->errmsg, _("Volume %s is busy swapping.\n"),
                  NPRT(vol->vol_name));
            }
            debug_list_volumes("failed swap");
            vol = NULL;                  /* device busy */
            goto get_out;
         }
      } else {
         dev->vol = vol;
      }
   } else {
      dev->vol = vol;                    /* point to newly inserted volume */
   }

get_out:
   if (vol) {
      Dmsg2(dbglvl, "set in_use. vol=%s dev=%s\n", vol->vol_name,
            vol->dev->print_name());
      vol->set_in_use();
      dcr->reserved_volume = true;
      bstrncpy(dcr->VolumeName, vol->vol_name, sizeof(dcr->VolumeName));
   }
   debug_list_volumes("end new volume");
   unlock_volumes();
   return vol;
}

/*
 * Start walk of vol chain
 * The proper way to walk the vol chain is:
 *    VOLRES *vol;
 *    foreach_vol(vol) {
 *      ...
 *    }
 *    endeach_vol(vol);
 *
 *  It is possible to leave out the endeach_vol(vol), but
 *   in that case, the last vol referenced must be explicitly
 *   released with:
 *
 *    free_vol_item(vol);
 *
 */
VOLRES *vol_walk_start()
{
   VOLRES *vol;
   lock_volumes();
   vol = (VOLRES *)vol_list->first();
   if (vol) {
      vol->inc_use_count();
      Dmsg2(dbglvl, "Inc walk_start use_count=%d volname=%s\n",
            vol->use_count(), vol->vol_name);
   }
   unlock_volumes();
   return vol;
}

/*
 * Get next vol from chain, and release current one
 */
VOLRES *vol_walk_next(VOLRES *prev_vol)
{
   VOLRES *vol;

   lock_volumes();
   vol = (VOLRES *)vol_list->next(prev_vol);
   if (vol) {
      vol->inc_use_count();
      Dmsg2(dbglvl, "Inc walk_next use_count=%d volname=%s\n",
            vol->use_count(), vol->vol_name);
   }
   if (prev_vol) {
      free_vol_item(prev_vol);
   }
   unlock_volumes();
   return vol;
}

/*
 * Release last vol referenced
 */
void vol_walk_end(VOLRES *vol)
{
   if (vol) {
      lock_volumes();
      Dmsg2(dbglvl, "Free walk_end use_count=%d volname=%s\n",
            vol->use_count(), vol->vol_name);
      free_vol_item(vol);
      unlock_volumes();
   }
}

/*
 * Search for a Volume name in the Volume list.
 *
 *  Returns: VOLRES entry on success
 *           NULL if the Volume is not in the list
 */
static VOLRES *find_volume(const char *VolumeName)
{
   VOLRES vol, *fvol;

   if (vol_list->empty()) {
      return NULL;
   }
   /* Do not lock reservations here */
   lock_volumes();
   vol.vol_name = bstrdup(VolumeName);
   fvol = (VOLRES *)vol_list->binary_search(&vol, name_compare);
   free(vol.vol_name);
   Dmsg2(dbglvl, "find_vol=%s found=%d\n", VolumeName, fvol!=NULL);
   debug_list_volumes("find_volume");
   unlock_volumes();
   return fvol;
}

/*
 * Search for a Volume name in the read Volume list.
 *
 *  Returns: VOLRES entry on success
 *           NULL if the Volume is not in the list
 */
static VOLRES *find_read_volume(const char *VolumeName)
{
   VOLRES vol, *fvol;

   if (read_vol_list->empty()) {
      Dmsg0(dbglvl, "find_read_vol: read_vol_list empty.\n");
      return NULL;
   }
   /* Do not lock reservations here */
   lock_read_volumes();
   vol.vol_name = bstrdup(VolumeName);
   /* Note, we do want a simple name_compare on volume name only here */
   fvol = (VOLRES *)read_vol_list->binary_search(&vol, name_compare);
   free(vol.vol_name);
   Dmsg2(dbglvl, "find_read_vol=%s found=%d\n", VolumeName, fvol!=NULL);
   unlock_read_volumes();
   return fvol;
}


/*
 * Free a Volume from the Volume list if it is no longer used
 *   Note, for tape drives we want to remember where the Volume
 *   was when last used, so rather than free the volume entry,
 *   we simply mark it "not reserved" so when the drive is really
 *   needed for another volume, we can reuse it.
 *
 *  Returns: true if the Volume found and "removed" from the list
 *           false if the Volume is not in the list or is in use
 */
bool volume_unused(DCR *dcr)
{
   DEVICE *dev = dcr->dev;

   if (!dev->vol) {
      Dmsg1(dbglvl, "vol_unused: no vol on %s\n", dev->print_name());
      debug_list_volumes("null vol cannot unreserve_volume");
      return false;
   }

   Dmsg2(dbglvl, "Clear in_use vol=%s slot=%d\n", dev->vol->vol_name,
         dev->vol->get_slot());
   dev->vol->clear_in_use();

   if (dev->vol->is_swapping()) {
      Dmsg1(dbglvl, "vol_unused: vol being swapped on %s\n", dev->print_name());
      debug_list_volumes("swapping vol cannot free_volume");
      return false;
   }

   /*
    * If this is a tape, we do not free the volume, rather we wait
    *  until the autoloader unloads it, or until another tape is
    *  explicitly read in this drive. This allows the SD to remember
    *  where the tapes are or last were.
    */
   Dmsg5(dbglvl, "set not reserved vol=%s slot=%d writers=%d reserves=%d dev=%s\n",
      dev->vol->vol_name, dev->vol->get_slot(), dev->num_writers,
      dev->num_reserved(), dev->print_name());
   if (dev->is_tape() || dev->is_autochanger()) {
      return true;
   } else {
      /*
       * Note, this frees the volume reservation entry, but the
       *   file descriptor remains open with the OS.
       */
      return free_volume(dev);
   }
}

/*
 * Unconditionally release the volume entry
 * Note: read volumes are not in the list, so
 *   do not attempt to remove them.
 */
bool free_volume(DEVICE *dev)
{
   VOLRES *vol;

   lock_volumes();
   vol = dev->vol;
   if (vol == NULL) {
      Dmsg1(dbglvl, "No vol on dev %s\n", dev->print_name());
      unlock_volumes();
      return false;
   }
   /* Don't free a volume while it is being swapped */
   if (!vol->is_swapping()) {
      Dmsg2(dbglvl, "Clear in_use vol=%s slot=%d\n", vol->vol_name, vol->get_slot());
      dev->vol = NULL;
      if (vol->is_writing()) {
         vol_list->remove(vol);
      }
      Dmsg3(dbglvl, "Remove volume %s slot=%d dev=%s\n", vol->vol_name,
         vol->get_slot(), dev->print_name());
      free_vol_item(vol);
      debug_list_volumes("free_volume");
   } else {
      Dmsg1(dbglvl, "=== Cannot clear. Swapping vol=%s\n", vol->vol_name);
   }
   unlock_volumes();
   return true;
}


/* Create the Volume list */
void create_volume_lists()
{
   VOLRES *vol = NULL;
   if (vol_list == NULL) {
      vol_list = New(dlist(vol, &vol->link));
   }
   if (read_vol_list == NULL) {
      read_vol_list = New(dlist(vol, &vol->link));
   }
}

/*
 * Free normal append volumes list
 */
static void free_volume_list()
{
   VOLRES *vol;
   if (vol_list) {
      lock_volumes();
      foreach_dlist(vol, vol_list) {
         if (vol->dev) {
            Dmsg2(dbglvl, "free vol_list Volume=%s dev=%s\n", vol->vol_name, vol->dev->print_name());
         } else {
            Dmsg1(dbglvl, "free vol_list Volume=%s No dev\n", vol->vol_name);
         }
         free(vol->vol_name);
         vol->vol_name = NULL;
         vol->destroy_mutex();
      }
      delete vol_list;
      vol_list = NULL;
      unlock_volumes();
   }
}

/* Release all Volumes from the list */
void free_volume_lists()
{
   VOLRES *vol;

   free_volume_list();           /* normal append list */

   if (read_vol_list) {
      lock_read_volumes();
      foreach_dlist(vol, read_vol_list) {
         if (vol->dev) {
            Dmsg2(dbglvl, "free read_vol_list Volume=%s dev=%s\n", vol->vol_name, vol->dev->print_name());
         } else {
            Dmsg1(dbglvl, "free read_vol_list Volume=%s No dev\n", vol->vol_name);
         }
         free(vol->vol_name);
         vol->vol_name = NULL;
         vol->destroy_mutex();
      }
      delete read_vol_list;
      read_vol_list = NULL;
      unlock_read_volumes();
   }
}

/*
 * Determine if caller can write on volume.
 *  If not, return reason in jcr->errmsg
 */
bool DCR::can_i_write_volume()
{
   VOLRES *vol;

   vol = find_read_volume(VolumeName);
   if (vol) {
      Mmsg(jcr->errmsg, "Found in read list; cannot write vol=%s\n", VolumeName);
      Dmsg1(100, "Found in read list; cannot write vol=%s\n", VolumeName);
      return false;
   }
   return can_i_use_volume();
}

/*
 * Determine if caller can read or write volume.
 *  If not, return reason in jcr->errmsg
 */
bool DCR::can_i_use_volume()
{
   bool rtn = true;
   VOLRES *vol;

   if (job_canceled(jcr)) {
      Mmsg(jcr->errmsg, "Job is canceled\n");
      return false;
   }
   lock_volumes();
   vol = find_volume(VolumeName);
   if (!vol) {
      Dmsg1(dbglvl, "Vol=%s not in use.\n", VolumeName);
      goto get_out;                   /* vol not in list */
   }
   ASSERT2(vol->dev != NULL, "No device in can_i_use_volume!");

   if (dev == vol->dev) {        /* same device OK */
      Dmsg1(dbglvl, "Vol=%s on same dev.\n", VolumeName);
      goto get_out;
   } else {
      Dmsg3(dbglvl, "Vol=%s on %s we have %s\n", VolumeName,
            vol->dev->print_name(), dev->print_name());
   }
   /* ***FIXME*** check this ... */
   if (!vol->dev->is_busy()) {
      Dmsg2(dbglvl, "Vol=%s dev=%s not busy.\n", VolumeName, vol->dev->print_name());
      goto get_out;
   } else {
      Dmsg2(dbglvl, "Vol=%s dev=%s busy.\n", VolumeName, vol->dev->print_name());
   }
   Mmsg(jcr->errmsg, "Volume=%s in use on another device %s.\n", VolumeName, vol->dev->print_name());
   Dmsg2(dbglvl, "Volume=%s in use on another device %s.\n", VolumeName, vol->dev->print_name());
   rtn = false;

get_out:
   unlock_volumes();
   return rtn;

}

/*
 * Create a temporary copy of the volume list.  We do this,
 *   to avoid having the volume list locked during the
 *   call to reserve_device(), which would cause a deadlock.
 * Note, we may want to add an update counter on the vol_list
 *   so that if it is modified while we are traversing the copy
 *   we can take note and act accordingly (probably redo the
 *   search at least a few times).
 */
dlist *dup_vol_list(JCR *jcr)
{
   dlist *temp_vol_list;
   VOLRES *vol = NULL;

   Dmsg0(dbglvl, "lock volumes\n");

   Dmsg0(dbglvl, "duplicate vol list\n");
   temp_vol_list = New(dlist(vol, &vol->link));
   foreach_vol(vol) {
      VOLRES *nvol;
      VOLRES *tvol = (VOLRES *)malloc(sizeof(VOLRES));
      memset(tvol, 0, sizeof(VOLRES));
      tvol->vol_name = bstrdup(vol->vol_name);
      tvol->dev = vol->dev;
      tvol->init_mutex();
      tvol->inc_use_count();
      nvol = (VOLRES *)temp_vol_list->binary_insert(tvol, name_compare);
      if (tvol != nvol) {
         tvol->dev = NULL;                   /* don't zap dev entry */
         free_vol_item(tvol);
         Pmsg0(000, "Logic error. Duplicating vol list hit duplicate.\n");
         Jmsg(jcr, M_WARNING, 0, "Logic error. Duplicating vol list hit duplicate.\n");
      }
   }
   endeach_vol(vol);
   Dmsg0(dbglvl, "unlock volumes\n");
   return temp_vol_list;
}

/*
 * Free the specified temp list.
 */
void free_temp_vol_list(dlist *temp_vol_list)
{
   dlist *save_vol_list;

   lock_volumes();
   save_vol_list = vol_list;
   vol_list = temp_vol_list;
   free_volume_list();                  /* release temp_vol_list */
   vol_list = save_vol_list;
   Dmsg0(dbglvl, "deleted temp vol list\n");
   Dmsg0(dbglvl, "unlock volumes\n");
   unlock_volumes();
   debug_list_volumes("after free temp table");
}
