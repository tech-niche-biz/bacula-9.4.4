/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2018 Kern Sibbald

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
 *  This file contains all the SQL commands that are either issued by
 *   the Director or which are database backend specific.
 *
 *     Written by Kern Sibbald, July MMII
 */
/*
 * Note, PostgreSQL imposes some constraints on using DISTINCT and GROUP BY
 *  for example, the following is illegal in PostgreSQL:
 *  SELECT DISTINCT JobId FROM temp ORDER BY StartTime ASC;
 *  because all the ORDER BY expressions must appear in the SELECT list!
 */

#include "bacula.h"

const char *get_restore_objects = 
   "SELECT JobId,ObjectLength,ObjectFullLength,ObjectIndex,"
           "ObjectType,ObjectCompression,FileIndex,ObjectName,"
           "RestoreObject,PluginName "
    "FROM RestoreObject "
   "WHERE JobId IN (%s) "
     "AND ObjectType = %d "
   "ORDER BY ObjectIndex ASC";

const char *cleanup_created_job =
   "UPDATE Job SET JobStatus='f', StartTime=SchedTime, EndTime=SchedTime "
   "WHERE JobStatus = 'C'";
const char *cleanup_running_job = 
   "UPDATE Job SET JobStatus='f', EndTime=StartTime WHERE JobStatus = 'R'";

/* For sql_update.c db_update_stats */
const char *fill_jobhisto =
        "INSERT INTO JobHisto (JobId, Job, Name, Type, Level,"
           "ClientId, JobStatus,"
           "SchedTime, StartTime, EndTime, RealEndTime, JobTDate,"
           "VolSessionId, VolSessionTime, JobFiles, JobBytes, ReadBytes,"
           "JobErrors, JobMissingFiles, PoolId, FileSetId, PriorJobId,"
           "PurgedFiles, HasBase, Reviewed, Comment)"
        "SELECT JobId, Job, Name, Type, Level, ClientId, JobStatus,"
           "SchedTime, StartTime, EndTime, RealEndTime, JobTDate,"
           "VolSessionId, VolSessionTime, JobFiles, JobBytes, ReadBytes,"
           "JobErrors, JobMissingFiles, PoolId, FileSetId, PriorJobId,"
           "PurgedFiles, HasBase, Reviewed, Comment "
          "FROM Job "
         "WHERE JobStatus IN ('T','W','f','A','E')"
           "AND NOT EXISTS "
                "(SELECT JobHisto.JobId "
                   "FROM JobHisto WHERE JobHisto.Jobid=Job.JobId)"
           "AND JobTDate < %s ";

/* For ua_update.c */
const char *list_pool = "SELECT * FROM Pool WHERE PoolId=%s";

/* For ua_dotcmds.c */
const char *client_backups =
   "SELECT DISTINCT Job.JobId,Client.Name as Client,Level,StartTime,"
   "JobFiles,JobBytes,VolumeName,MediaType,FileSet,Media.Enabled as Enabled"
   " FROM Client,Job,JobMedia,Media,FileSet"
   " WHERE Client.Name='%s'"
   " AND FileSet='%s'"
   " AND Client.ClientId=Job.ClientId "
   " AND JobStatus IN ('T','W') AND Type='B' "
   " AND JobMedia.JobId=Job.JobId AND JobMedia.MediaId=Media.MediaId "
   " AND Job.FileSetId=FileSet.FileSetId"
   " ORDER BY Job.StartTime";

/* ====== ua_prune.c */

const char *sel_JobMedia = 
   "SELECT DISTINCT JobMedia.JobId FROM JobMedia,Job"
   " WHERE MediaId=%s AND Job.JobId=JobMedia.JobId "
   " AND Job.JobTDate<%s AND Job.JobStatus NOT IN ('R', 'C') ";

/* Delete temp tables and indexes  */
const char *drop_deltabs[] = {
   "DROP TABLE DelCandidates",
   NULL};

const char *create_delindex = "CREATE INDEX DelInx1 ON DelCandidates (JobId)";

/* ======= ua_restore.c */
const char *uar_count_files =
   "SELECT JobFiles FROM Job WHERE JobId=%s";

/* List last 20 Jobs */
const char *uar_list_jobs =
   "SELECT JobId,Client.Name as Client,StartTime,Level as "
   "JobLevel,JobFiles,JobBytes "
   "FROM Client,Job WHERE Client.ClientId=Job.ClientId AND JobStatus IN ('T','W') "
   "AND Type='B' ORDER BY StartTime DESC LIMIT 20";

const char *uar_print_jobs = 
   "SELECT DISTINCT JobId,Level,JobFiles,JobBytes,StartTime,VolumeName"
   " FROM Job JOIN JobMedia USING (JobId) JOIN Media USING (MediaId) "
   " WHERE JobId IN (%s) "
   " ORDER BY StartTime ASC";

/*
 * Find all files for a particular JobId and insert them into
 *  the tree during a restore.
 */
const char *uar_sel_files =
   "SELECT Path.Path,Filename.Name,FileIndex,JobId,LStat "
   "FROM File,Filename,Path "
   "WHERE File.JobId IN (%s) AND Filename.FilenameId=File.FilenameId "
   "AND Path.PathId=File.PathId";

const char *uar_del_temp  = "DROP TABLE temp";
const char *uar_del_temp1 = "DROP TABLE temp1";

const char *uar_last_full =
   "INSERT INTO temp1 SELECT Job.JobId,JobTdate "
   "FROM Client,Job,JobMedia,Media,FileSet WHERE Client.ClientId=%s "
   "AND Job.ClientId=%s "
   "AND Job.StartTime < '%s' "
   "AND Level='F' AND JobStatus IN ('T','W') AND Type='B' "
   "AND JobMedia.JobId=Job.JobId "
   "AND Media.Enabled=1 "
   "AND JobMedia.MediaId=Media.MediaId "
   "AND Job.FileSetId=FileSet.FileSetId "
   "AND FileSet.FileSet='%s' "
   "%s"
   "ORDER BY Job.JobTDate DESC LIMIT 1";

const char *uar_full =
   "INSERT INTO temp SELECT Job.JobId,Job.JobTDate,"
   "Job.ClientId,Job.Level,Job.JobFiles,Job.JobBytes,"
   "StartTime,VolumeName,JobMedia.StartFile,VolSessionId,VolSessionTime "
   "FROM temp1,Job,JobMedia,Media WHERE temp1.JobId=Job.JobId "
   "AND Level='F' AND JobStatus IN ('T','W') AND Type='B' "
   "AND Media.Enabled=1 "
   "AND JobMedia.JobId=Job.JobId "
   "AND JobMedia.MediaId=Media.MediaId";

const char *uar_dif =
   "INSERT INTO temp SELECT Job.JobId,Job.JobTDate,Job.ClientId,"
   "Job.Level,Job.JobFiles,Job.JobBytes,"
   "Job.StartTime,Media.VolumeName,JobMedia.StartFile,"
   "Job.VolSessionId,Job.VolSessionTime "
   "FROM Job,JobMedia,Media,FileSet "
   "WHERE Job.JobTDate>%s AND Job.StartTime<'%s' "
   "AND Job.ClientId=%s "
   "AND JobMedia.JobId=Job.JobId "
   "AND Media.Enabled=1 "
   "AND JobMedia.MediaId=Media.MediaId "
   "AND Job.Level='D' AND JobStatus IN ('T','W') AND Type='B' "
   "AND Job.FileSetId=FileSet.FileSetId "
   "AND FileSet.FileSet='%s' "
   "%s"
   "ORDER BY Job.JobTDate DESC LIMIT 1";

const char *uar_inc =
   "INSERT INTO temp SELECT Job.JobId,Job.JobTDate,Job.ClientId,"
   "Job.Level,Job.JobFiles,Job.JobBytes,"
   "Job.StartTime,Media.VolumeName,JobMedia.StartFile,"
   "Job.VolSessionId,Job.VolSessionTime "
   "FROM Job,JobMedia,Media,FileSet "
   "WHERE Job.JobTDate>%s AND Job.StartTime<'%s' "
   "AND Job.ClientId=%s "
   "AND Media.Enabled=1 "
   "AND JobMedia.JobId=Job.JobId "
   "AND JobMedia.MediaId=Media.MediaId "
   "AND Job.Level='I' AND JobStatus IN ('T','W') AND Type='B' "
   "AND Job.FileSetId=FileSet.FileSetId "
   "AND FileSet.FileSet='%s' "
   "%s";

const char *uar_list_temp =
   "SELECT DISTINCT JobId,Level,JobFiles,JobBytes,StartTime,VolumeName"
   " FROM temp"
   " ORDER BY StartTime ASC";


const char *uar_sel_jobid_temp = 
   "SELECT DISTINCT JobId,StartTime FROM temp ORDER BY StartTime ASC";

const char *uar_sel_all_temp1 = "SELECT * FROM temp1";

const char *uar_sel_all_temp = "SELECT * FROM temp";



/* Select FileSet names for this Client */
const char *uar_sel_fileset =
   "SELECT DISTINCT FileSet.FileSet FROM Job,"
   "Client,FileSet WHERE Job.FileSetId=FileSet.FileSetId "
   "AND Job.ClientId=%s AND Client.ClientId=%s "
   "ORDER BY FileSet.FileSet";

/* Select all different FileSet for this client
 * This query doesn't guarantee that the Id is the latest
 * version of the FileSet. Can be used with other queries that
 * use Ids to select the FileSet name. (like in accurate)
 */
const char *uar_sel_filesetid =
   "SELECT MAX(FileSet.FileSetId) "
     "FROM FileSet JOIN Job USING (FileSetId) "
         "WHERE Job.ClientId=%s "
        "GROUP BY FileSet";

/*
 *  Find JobId, FileIndex for a given path/file and date
 *  for use when inserting individual files into the tree.
 */
const char *uar_jobid_fileindex =
   "SELECT Job.JobId,File.FileIndex FROM Job,File,Path,Filename,Client "
   "WHERE Job.JobId=File.JobId "
   "AND Job.StartTime<='%s' "
   "AND Path.Path='%s' "
   "AND Filename.Name='%s' "
   "AND Client.Name='%s' "
   "AND Job.ClientId=Client.ClientId "
   "AND Path.PathId=File.PathId "
   "AND Filename.FilenameId=File.FilenameId "
   "AND JobStatus IN ('T','W') AND Type='B' "
   "ORDER BY Job.StartTime DESC LIMIT 1";

const char *uar_jobids_fileindex =
   "SELECT Job.JobId,File.FileIndex FROM Job,File,Path,Filename,Client "
   "WHERE Job.JobId IN (%s) "
   "AND Job.JobId=File.JobId "
   "AND Job.StartTime<='%s' "
   "AND Path.Path='%s' "
   "AND Filename.Name='%s' "
   "AND Client.Name='%s' "
   "AND Job.ClientId=Client.ClientId "
   "AND Path.PathId=File.PathId "
   "AND Filename.FilenameId=File.FilenameId "
   "ORDER BY Job.StartTime DESC LIMIT 1";

/* Query to get list of files from table -- presuably built by an external program */
const char *uar_jobid_fileindex_from_table = 
   "SELECT JobId, FileIndex FROM %s ORDER BY JobId, FileIndex ASC";

/* Get the list of the last recent version per Delta with a given
 *  jobid list. This is a tricky part because with SQL the result of:
 *
 *   SELECT MAX(A), B, C, D FROM... GROUP BY (B,C)
 *
 * doesn't give the good result (for D).
 *
 * With PostgreSQL, we can use DISTINCT ON(), but with Mysql or Sqlite,
 *  we need an extra join using JobTDate.
 */
static const char *select_recent_version_with_basejob_default =
"SELECT FileId, Job.JobId AS JobId, FileIndex, File.PathId AS PathId, "
       "File.FilenameId AS FilenameId, LStat, MD5, DeltaSeq, "
       "Job.JobTDate AS JobTDate "
"FROM Job, File, ( "
    "SELECT MAX(JobTDate) AS JobTDate, PathId, FilenameId "
      "FROM ( "
        "SELECT JobTDate, PathId, FilenameId "   /* Get all normal files */
          "FROM File JOIN Job USING (JobId) "    /* from selected backup */
         "WHERE File.JobId IN (%s) "
          "UNION ALL "
        "SELECT JobTDate, PathId, FilenameId "   /* Get all files from */
          "FROM BaseFiles "                      /* BaseJob */
               "JOIN File USING (FileId) "
               "JOIN Job  ON    (BaseJobId = Job.JobId) "
         "WHERE BaseFiles.JobId IN (%s) "        /* Use Max(JobTDate) to find */
       ") AS tmp "
       "GROUP BY PathId, FilenameId "            /* the latest file version */
    ") AS T1 "
"WHERE (Job.JobId IN ( "  /* Security, we force JobId to be valid */
        "SELECT DISTINCT BaseJobId FROM BaseFiles WHERE JobId IN (%s)) "
        "OR Job.JobId IN (%s)) "
  "AND T1.JobTDate = Job.JobTDate " /* Join on JobTDate to get the orginal */
  "AND Job.JobId = File.JobId "     /* Job/File record */
  "AND T1.PathId = File.PathId "
  "AND T1.FilenameId = File.FilenameId";

const char *select_recent_version_with_basejob[] =
{
   /* MySQL  */
   select_recent_version_with_basejob_default,

   /* Postgresql */    /* The DISTINCT ON () permits to avoid extra join */
   "SELECT DISTINCT ON (FilenameId, PathId) JobTDate, JobId, FileId, "
         "FileIndex, PathId, FilenameId, LStat, MD5, DeltaSeq "
   "FROM "
     "(SELECT FileId, JobId, PathId, FilenameId, FileIndex, LStat, MD5, DeltaSeq "
         "FROM File WHERE JobId IN (%s) "
        "UNION ALL "
       "SELECT File.FileId, File.JobId, PathId, FilenameId, "
              "File.FileIndex, LStat, MD5, DeltaSeq "
         "FROM BaseFiles JOIN File USING (FileId) "
        "WHERE BaseFiles.JobId IN (%s) "
       ") AS T JOIN Job USING (JobId) "
   "ORDER BY FilenameId, PathId, JobTDate DESC ",

   /* SQLite */
   select_recent_version_with_basejob_default
};
 
/* We do the same thing than the previous query, but we include
 * all delta parts. If the file has been deleted, we can have irrelevant
 * parts.
 *
 * The code that uses results should control the delta sequence with
 * the following rules:
 * First Delta = 0
 * Delta = Previous Delta + 1
 *
 * If we detect a gap, we can discard further pieces
 * If a file starts at 1 instead of 0, the file has been deleted, and further
 *   pieces are useless.
 * This control should be reset for each new file
 */
static const char *select_recent_version_with_basejob_and_delta_default =
"SELECT FileId, Job.JobId AS JobId, FileIndex, File.PathId AS PathId, "
       "File.FilenameId AS FilenameId, LStat, MD5, File.DeltaSeq AS DeltaSeq, "
       "Job.JobTDate AS JobTDate "
"FROM Job, File, ( "
    "SELECT MAX(JobTDate) AS JobTDate, PathId, FilenameId, DeltaSeq "
      "FROM ( "
       "SELECT JobTDate, PathId, FilenameId, DeltaSeq " /*Get all normal files*/
         "FROM File JOIN Job USING (JobId) "          /* from selected backup */
        "WHERE File.JobId IN (%s) "
         "UNION ALL "
       "SELECT JobTDate, PathId, FilenameId, DeltaSeq " /*Get all files from */
         "FROM BaseFiles "                            /* BaseJob */
              "JOIN File USING (FileId) "
              "JOIN Job  ON    (BaseJobId = Job.JobId) "
        "WHERE BaseFiles.JobId IN (%s) "        /* Use Max(JobTDate) to find */
       ") AS tmp "
       "GROUP BY PathId, FilenameId, DeltaSeq "    /* the latest file version */
    ") AS T1 "
"WHERE (Job.JobId IN ( "  /* Security, we force JobId to be valid */
        "SELECT DISTINCT BaseJobId FROM BaseFiles WHERE JobId IN (%s)) "
        "OR Job.JobId IN (%s)) "
  "AND T1.JobTDate = Job.JobTDate " /* Join on JobTDate to get the orginal */
  "AND Job.JobId = File.JobId "     /* Job/File record */
  "AND T1.PathId = File.PathId "
  "AND T1.FilenameId = File.FilenameId";

const char *select_recent_version_with_basejob_and_delta[] = {
   /* MySQL */
   select_recent_version_with_basejob_and_delta_default,

   /* Postgresql */    /* The DISTINCT ON () permits to avoid extra join */
   "SELECT DISTINCT ON (FilenameId, PathId, DeltaSeq) JobTDate, JobId, FileId, "
         "FileIndex, PathId, FilenameId, LStat, MD5, DeltaSeq "
   "FROM "
    "(SELECT FileId, JobId, PathId, FilenameId, FileIndex, LStat, MD5,DeltaSeq "
         "FROM File WHERE JobId IN (%s) "
        "UNION ALL "
       "SELECT File.FileId, File.JobId, PathId, FilenameId, "
              "File.FileIndex, LStat, MD5, DeltaSeq "
         "FROM BaseFiles JOIN File USING (FileId) "
        "WHERE BaseFiles.JobId IN (%s) "
       ") AS T JOIN Job USING (JobId) "
   "ORDER BY FilenameId, PathId, DeltaSeq, JobTDate DESC ",

   /* SQLite */
   select_recent_version_with_basejob_and_delta_default
};

/* Get the list of the last recent version with a given BaseJob jobid list
 * We don't handle Delta with BaseJobs, they have only Full files
 */
static const char *select_recent_version_default = 
  "SELECT j1.JobId AS JobId, f1.FileId AS FileId, f1.FileIndex AS FileIndex, "
          "f1.PathId AS PathId, f1.FilenameId AS FilenameId, "
          "f1.LStat AS LStat, f1.MD5 AS MD5, j1.JobTDate "
     "FROM ( "     /* Choose the last version for each Path/Filename */
       "SELECT max(JobTDate) AS JobTDate, PathId, FilenameId "
         "FROM File JOIN Job USING (JobId) "
        "WHERE File.JobId IN (%s) "
       "GROUP BY PathId, FilenameId "
     ") AS t1, Job AS j1, File AS f1 "
    "WHERE t1.JobTDate = j1.JobTDate "
      "AND j1.JobId IN (%s) "
      "AND t1.FilenameId = f1.FilenameId "
      "AND t1.PathId = f1.PathId "
      "AND j1.JobId = f1.JobId";

const char *select_recent_version[] =
{
   /* MySQL */
   select_recent_version_default,

   /* Postgresql */
   "SELECT DISTINCT ON (FilenameId, PathId) JobTDate, JobId, FileId, "
          "FileIndex, PathId, FilenameId, LStat, MD5 "
     "FROM File JOIN Job USING (JobId) "
    "WHERE JobId IN (%s) "
    "ORDER BY FilenameId, PathId, JobTDate DESC ",

   /* SQLite */
   select_recent_version_default
};

/* We don't create this table as TEMPORARY because MySQL 
    MyISAM 5.0 and 5.1 are unable to run further queries in this mode
 */
static const char *create_temp_accurate_jobids_default = 
 "CREATE TABLE btemp3%s AS "
    "SELECT JobId, StartTime, EndTime, JobTDate, PurgedFiles "
      "FROM Job JOIN FileSet USING (FileSetId) "
     "WHERE ClientId = %s "
       "AND Level='F' AND JobStatus IN ('T','W') AND Type='B' "
       "AND StartTime<'%s' "
       "AND FileSet.FileSet=(SELECT FileSet FROM FileSet WHERE FileSetId = %s) "
       " %s "                   /* Any filter */
     "ORDER BY Job.JobTDate DESC LIMIT 1";

const char *create_temp_accurate_jobids[] = {
   /* Mysql */
   create_temp_accurate_jobids_default,

   /* Postgresql */
   create_temp_accurate_jobids_default,

   /* SQLite3 */
   create_temp_accurate_jobids_default
};

const char *create_temp_basefile[] = {
   /* Mysql */
   "CREATE TEMPORARY TABLE basefile%lld ("
   "Path BLOB NOT NULL,"
   "Name BLOB NOT NULL,"
   "INDEX (Path(255), Name(255)))",

   /* Postgresql */
   "CREATE TEMPORARY TABLE basefile%lld ("
   "Path TEXT,"
   "Name TEXT)",

   /* SQLite3 */
   "CREATE TEMPORARY TABLE basefile%lld ("
   "Path TEXT,"
   "Name TEXT)"
};

const char *create_temp_new_basefile[] = {
   /* Mysql */
   "CREATE TEMPORARY TABLE new_basefile%lld AS "
   "SELECT Path.Path AS Path, Filename.Name AS Name, Temp.FileIndex AS FileIndex,"
   "Temp.JobId AS JobId, Temp.LStat AS LStat, Temp.FileId AS FileId, "
   "Temp.MD5 AS MD5 "
   "FROM ( %s ) AS Temp "
   "JOIN Filename ON (Filename.FilenameId = Temp.FilenameId) "
   "JOIN Path ON (Path.PathId = Temp.PathId) "
   "WHERE Temp.FileIndex > 0",

   /* Postgresql */
   "CREATE TEMPORARY TABLE new_basefile%lld AS "
   "SELECT Path.Path AS Path, Filename.Name AS Name, Temp.FileIndex AS FileIndex,"
   "Temp.JobId AS JobId, Temp.LStat AS LStat, Temp.FileId AS FileId, "
   "Temp.MD5 AS MD5 "
   "FROM ( %s ) AS Temp "
   "JOIN Filename ON (Filename.FilenameId = Temp.FilenameId) "
   "JOIN Path ON (Path.PathId = Temp.PathId) "
   "WHERE Temp.FileIndex > 0",

   /* SQLite3 */
   "CREATE TEMPORARY TABLE new_basefile%lld AS "
   "SELECT Path.Path AS Path, Filename.Name AS Name, Temp.FileIndex AS FileIndex,"
   "Temp.JobId AS JobId, Temp.LStat AS LStat, Temp.FileId AS FileId, "
   "Temp.MD5 AS MD5 "
   "FROM ( %s ) AS Temp "
   "JOIN Filename ON (Filename.FilenameId = Temp.FilenameId) "
   "JOIN Path ON (Path.PathId = Temp.PathId) "
   "WHERE Temp.FileIndex > 0"
};

/* ====== ua_prune.c */

/* List of SQL commands to create temp table and indicies  */
const char *create_deltabs[] =
{
   /* MySQL */
   "CREATE TEMPORARY TABLE DelCandidates ("
   "JobId INTEGER UNSIGNED NOT NULL, "
   "PurgedFiles TINYINT, "
   "FileSetId INTEGER UNSIGNED, "
   "JobFiles INTEGER UNSIGNED, "
   "JobStatus BINARY(1))",
 
   /* PostgreSQL */
   "CREATE TEMPORARY TABLE DelCandidates ( "
   "JobId INTEGER NOT NULL, "
   "PurgedFiles SMALLINT, "
   "FileSetId INTEGER, "
   "JobFiles INTEGER, "
   "JobStatus char(1))",
 
   /* SQLite */
   "CREATE TEMPORARY TABLE DelCandidates ("
   "JobId INTEGER UNSIGNED NOT NULL, "
   "PurgedFiles TINYINT, "
   "FileSetId INTEGER UNSIGNED, "
   "JobFiles INTEGER UNSIGNED, "
   "JobStatus CHAR)"
}; 

/* ======= ua_purge.c ====== */
/* Select the first available Copy Job that must be upgraded 
 *   to a Backup job when the original backup job is expired.
 */
static const char *uap_upgrade_copies_oldest_job_default = 
"CREATE TEMPORARY TABLE cpy_tmp AS "
       "SELECT MIN(JobId) AS JobId FROM Job "     /* Choose the oldest job */
        "WHERE Type='%c' "                        /* JT_JOB_COPY */
          "AND ( PriorJobId IN (%s) "             /* JobId selection */
              "OR "
               " PriorJobId IN ( "
                  "SELECT PriorJobId "
                    "FROM Job "
                   "WHERE JobId IN (%s) "         /* JobId selection */
                    " AND Type='B' "
                 ") "
              ") "
          "GROUP BY PriorJobId ";           /* one result per copy */

const char *uap_upgrade_copies_oldest_job[] =
{
   /* MySQL */
   uap_upgrade_copies_oldest_job_default,
   /* PostgreSQL */
   uap_upgrade_copies_oldest_job_default,
   /* SQLite */
   uap_upgrade_copies_oldest_job_default
}; 
 
/* ======= ua_restore.c ====== */

/* List Jobs where a particular file is saved */
const char *uar_file[] =
{
   /* MySQL */
   "SELECT Job.JobId as JobId,"
   "CONCAT(Path.Path,Filename.Name) as Name, "
   "StartTime,Type as JobType,JobStatus,JobFiles,JobBytes "
   "FROM Client,Job,File,Filename,Path WHERE Client.Name='%s' "
   "AND Client.ClientId=Job.ClientId "
   "AND Job.JobId=File.JobId AND File.FileIndex > 0 "
   "AND Path.PathId=File.PathId AND Filename.FilenameId=File.FilenameId "
   "AND Filename.Name='%s' ORDER BY StartTime DESC LIMIT 20",

   /* Postgresql */
   "SELECT Job.JobId as JobId,"
   "Path.Path||Filename.Name as Name, "
   "StartTime,Type as JobType,JobStatus,JobFiles,JobBytes "
   "FROM Client,Job,File,Filename,Path WHERE Client.Name='%s' "
   "AND Client.ClientId=Job.ClientId "
   "AND Job.JobId=File.JobId AND File.FileIndex > 0 "
   "AND Path.PathId=File.PathId AND Filename.FilenameId=File.FilenameId "
   "AND Filename.Name='%s' ORDER BY StartTime DESC LIMIT 20",

   /* SQLite3 */
   "SELECT Job.JobId as JobId,"
   "Path.Path||Filename.Name as Name, "
   "StartTime,Type as JobType,JobStatus,JobFiles,JobBytes "
   "FROM Client,Job,File,Filename,Path WHERE Client.Name='%s' "
   "AND Client.ClientId=Job.ClientId "
   "AND Job.JobId=File.JobId AND File.FileIndex > 0 "
   "AND Path.PathId=File.PathId AND Filename.FilenameId=File.FilenameId "
   "AND Filename.Name='%s' ORDER BY StartTime DESC LIMIT 20"
};

const char *uar_create_temp[] = {
   /* Mysql */
   "CREATE TEMPORARY TABLE temp ("
   "JobId INTEGER UNSIGNED NOT NULL,"
   "JobTDate BIGINT UNSIGNED,"
   "ClientId INTEGER UNSIGNED,"
   "Level CHAR,"
   "JobFiles INTEGER UNSIGNED,"
   "JobBytes BIGINT UNSIGNED,"
   "StartTime TEXT,"
   "VolumeName TEXT,"
   "StartFile INTEGER UNSIGNED,"
   "VolSessionId INTEGER UNSIGNED,"
   "VolSessionTime INTEGER UNSIGNED)",
 
   /* PostgreSQL */
   "CREATE TEMPORARY TABLE temp ("
   "JobId INTEGER NOT NULL,"
   "JobTDate BIGINT,"
   "ClientId INTEGER,"
   "Level CHAR,"
   "JobFiles INTEGER,"
   "JobBytes BIGINT,"
   "StartTime TEXT,"
   "VolumeName TEXT,"
   "StartFile INTEGER,"
   "VolSessionId INTEGER,"
   "VolSessionTime INTEGER)",
 
   /* SQLite */
   "CREATE TEMPORARY TABLE temp ("
   "JobId INTEGER UNSIGNED NOT NULL,"
   "JobTDate BIGINT UNSIGNED,"
   "ClientId INTEGER UNSIGNED,"
   "Level CHAR,"
   "JobFiles INTEGER UNSIGNED,"
   "JobBytes BIGINT UNSIGNED,"
   "StartTime TEXT,"
   "VolumeName TEXT,"
   "StartFile INTEGER UNSIGNED,"
   "VolSessionId INTEGER UNSIGNED,"
   "VolSessionTime INTEGER UNSIGNED)"
}; 

const char *uar_create_temp1[] =
{
   /* MySQL */
   "CREATE TEMPORARY TABLE temp1 ("
   "JobId INTEGER UNSIGNED NOT NULL,"
   "JobTDate BIGINT UNSIGNED)",
   /* PostgreSQL */
   "CREATE TEMPORARY TABLE temp1 ("
   "JobId INTEGER NOT NULL,"
   "JobTDate BIGINT)",
   /* SQLite */
   "CREATE TEMPORARY TABLE temp1 ("
   "JobId INTEGER UNSIGNED NOT NULL,"
   "JobTDate BIGINT UNSIGNED)"
}; 

/* Query to get all files in a directory no recursing
 *  Note, for PostgreSQL since it respects the "Single Value
 *  rule", the results of the SELECT will be unoptimized.
 *  I.e. the same file will be restored multiple times, once
 *  for each time it was backed up.
 */

const char *uar_jobid_fileindex_from_dir[] = {
   /* Mysql */
   "SELECT Job.JobId,File.FileIndex FROM Job,File,Path,Filename,Client "
   "WHERE Job.JobId IN (%s) "
   "AND Job.JobId=File.JobId "
   "AND Path.Path='%s' "
   "AND Client.Name='%s' "
   "AND Job.ClientId=Client.ClientId "
   "AND Path.PathId=File.Pathid "
   "AND Filename.FilenameId=File.FilenameId "
   "GROUP BY File.FileIndex ",

   /* Postgresql */
   "SELECT Job.JobId,File.FileIndex FROM Job,File,Path,Filename,Client "
   "WHERE Job.JobId IN (%s) "
   "AND Job.JobId=File.JobId "
   "AND Path.Path='%s' "
   "AND Client.Name='%s' "
   "AND Job.ClientId=Client.ClientId "
   "AND Path.PathId=File.Pathid "
   "AND Filename.FilenameId=File.FilenameId",

   /* SQLite3 */
   "SELECT Job.JobId,File.FileIndex FROM Job,File,Path,Filename,Client "
   "WHERE Job.JobId IN (%s) "
   "AND Job.JobId=File.JobId "
   "AND Path.Path='%s' "
   "AND Client.Name='%s' "
   "AND Job.ClientId=Client.ClientId "
   "AND Path.PathId=File.Pathid "
   "AND Filename.FilenameId=File.FilenameId "
   "GROUP BY File.FileIndex "
};

const char *sql_media_order_most_recently_written[] = {
   /* Mysql */
   "ORDER BY LastWritten IS NULL,LastWritten DESC,MediaId",

   /* Postgresql */
   "ORDER BY LastWritten IS NULL,LastWritten DESC,MediaId",

   /* SQLite3 */
   "ORDER BY LastWritten IS NULL,LastWritten DESC,MediaId"
};

const char *sql_get_max_connections[] = {
   /* Mysql */
   "SHOW VARIABLES LIKE 'max_connections'",
   /* PostgreSQL */
   "SHOW max_connections",
   /* SQLite */
   "SELECT  0"
};

/*
 *  The Group By can return strange numbers when having multiple
 *  version of a file in the same dataset.
 */
const char *default_sql_bvfs_select =
"CREATE TABLE %s AS "
"SELECT File.JobId, File.FileIndex, File.FileId "
"FROM Job, File, ( "
    "SELECT MAX(JobTDate) AS JobTDate, PathId, FilenameId "
       "FROM btemp%s GROUP BY PathId, FilenameId "
    ") AS T1 JOIN Filename USING (FilenameId) "
"WHERE T1.JobTDate = Job.JobTDate "
  "AND Job.JobId = File.JobId "
  "AND T1.PathId = File.PathId "
  "AND T1.FilenameId = File.FilenameId "
  "AND File.FileIndex > 0 "
  "AND Job.JobId IN (SELECT DISTINCT JobId FROM btemp%s) ";

const char *sql_bvfs_select[] =
{
   /* MySQL */
   default_sql_bvfs_select,
   /* PostgreSQL */
   "CREATE TABLE %s AS ( "
        "SELECT JobId, FileIndex, FileId "
          "FROM ( "
             "SELECT DISTINCT ON (PathId, FilenameId) "
                    "JobId, FileIndex, FileId "
               "FROM btemp%s "
              "ORDER BY PathId, FilenameId, JobTDate DESC "
          ") AS T "
          "WHERE FileIndex > 0)",
   /* SQLite */
   default_sql_bvfs_select
};

static const char *sql_bvfs_list_files_default =
"SELECT 'F', T1.PathId, T1.FilenameId, Filename.Name, "
        "File.JobId, File.LStat, File.FileId "
"FROM Job, File, ( "
    "SELECT MAX(JobTDate) AS JobTDate, PathId, FilenameId "
      "FROM ( "
        "SELECT JobTDate, PathId, FilenameId "
          "FROM File JOIN Job USING (JobId) "
         "WHERE File.JobId IN (%s) AND PathId = %s "
          "UNION ALL "
        "SELECT JobTDate, PathId, FilenameId "
          "FROM BaseFiles "
               "JOIN File USING (FileId) "
               "JOIN Job  ON    (BaseJobId = Job.JobId) "
         "WHERE BaseFiles.JobId IN (%s)   AND PathId = %s "
       ") AS tmp GROUP BY PathId, FilenameId "
     "LIMIT %lld OFFSET %lld"
    ") AS T1 JOIN Filename USING (FilenameId) "
"WHERE T1.JobTDate = Job.JobTDate "
  "AND Job.JobId = File.JobId "
  "AND T1.PathId = File.PathId "
  "AND T1.FilenameId = File.FilenameId "
  "AND Filename.Name != '' "
  "AND File.FileIndex > 0 "
  " %s "                     /* AND Name LIKE '' */
  "AND (Job.JobId IN ( "
        "SELECT DISTINCT BaseJobId FROM BaseFiles WHERE JobId IN (%s)) "
       "OR Job.JobId IN (%s)) ";

const char *sql_bvfs_list_files[] = {
   /* MySQL */
   sql_bvfs_list_files_default,

   /* JobId PathId JobId PathId WHERE? Filename? Limit Offset*/
   /* Postgresql */
 "SELECT Type, PathId, FilenameId, Name, JobId, LStat, FileId "
  "FROM ("
   "SELECT DISTINCT ON (FilenameId) 'F' as Type, PathId, T.FilenameId, "
    "Filename.Name, JobId, LStat, FileId, FileIndex "
     "FROM "
         "(SELECT FileId, JobId, PathId, FilenameId, FileIndex, LStat, MD5 "
            "FROM File WHERE JobId IN (%s) AND PathId = %s "
           "UNION ALL "
          "SELECT File.FileId, File.JobId, PathId, FilenameId, "
                 "File.FileIndex, LStat, MD5 "
            "FROM BaseFiles JOIN File USING (FileId) "
           "WHERE BaseFiles.JobId IN (%s) AND File.PathId = %s "
          ") AS T JOIN Job USING (JobId) JOIN Filename USING (FilenameId) "
          " WHERE Filename.Name != '' "
          " %s "               /* AND Name LIKE '' */
     "ORDER BY FilenameId, StartTime DESC "
   ") AS A WHERE A.FileIndex > 0 "
   "LIMIT %lld OFFSET %lld ",

   /* SQLite */
   sql_bvfs_list_files_default,

   /* SQLite */
   sql_bvfs_list_files_default
};

/* Basically the same thing than select_recent_version_with_basejob_and_delta_default,
 * but we specify a single file with FilenameId/PathId
 * 
 * Input:
 * 1 JobId to look at
 * 2 FilenameId
 * 3 PathId
 * 4 JobId to look at
 * 5 FilenameId
 * 6 PathId
 * 7 Jobid
 * 8 JobId
 */
const char *bvfs_select_delta_version_with_basejob_and_delta_default =
"SELECT FileId, Job.JobId AS JobId, FileIndex, File.PathId AS PathId, "
       "File.FilenameId AS FilenameId, LStat, MD5, File.DeltaSeq AS DeltaSeq, "
       "Job.JobTDate AS JobTDate "
"FROM Job, File, ( "
    "SELECT MAX(JobTDate) AS JobTDate, PathId, FilenameId, DeltaSeq "
      "FROM ( "
       "SELECT JobTDate, PathId, FilenameId, DeltaSeq " /*Get all normal files*/
         "FROM File JOIN Job USING (JobId) "          /* from selected backup */
        "WHERE File.JobId IN (%s) AND FilenameId = %s AND PathId = %s "
         "UNION ALL "
       "SELECT JobTDate, PathId, FilenameId, DeltaSeq " /*Get all files from */ 
         "FROM BaseFiles "                            /* BaseJob */
              "JOIN File USING (FileId) "
              "JOIN Job  ON    (BaseJobId = Job.JobId) "
        "WHERE BaseFiles.JobId IN (%s) "        /* Use Max(JobTDate) to find */
             " AND FilenameId = %s AND PathId = %s "
       ") AS tmp "
       "GROUP BY PathId, FilenameId, DeltaSeq "    /* the latest file version */
    ") AS T1 "
"WHERE (Job.JobId IN ( "  /* Security, we force JobId to be valid */
        "SELECT DISTINCT BaseJobId FROM BaseFiles WHERE JobId IN (%s)) "
        "OR Job.JobId IN (%s)) "
  "AND T1.JobTDate = Job.JobTDate " /* Join on JobTDate to get the orginal */
  "AND Job.JobId = File.JobId "     /* Job/File record */
  "AND T1.PathId = File.PathId "
  "AND T1.FilenameId = File.FilenameId";


const char *bvfs_select_delta_version_with_basejob_and_delta[] =
{
   /* MySQL */
   bvfs_select_delta_version_with_basejob_and_delta_default,

   /* Postgresql */    /* The DISTINCT ON () permits to avoid extra join */
   "SELECT DISTINCT ON (FilenameId, PathId, DeltaSeq) JobTDate, JobId, FileId, "
         "FileIndex, PathId, FilenameId, LStat, MD5, DeltaSeq "
   "FROM "
    "(SELECT FileId, JobId, PathId, FilenameId, FileIndex, LStat, MD5,DeltaSeq "
         "FROM File WHERE JobId IN (%s) AND FilenameId = %s AND PathId = %s "
        "UNION ALL "
       "SELECT File.FileId, File.JobId, PathId, FilenameId, "
              "File.FileIndex, LStat, MD5, DeltaSeq "
         "FROM BaseFiles JOIN File USING (FileId) "
        "WHERE BaseFiles.JobId IN (%s) AND FilenameId = %s AND PathId = %s "
       ") AS T JOIN Job USING (JobId) "
   "ORDER BY FilenameId, PathId, DeltaSeq, JobTDate DESC ",

   /* SQLite */
   bvfs_select_delta_version_with_basejob_and_delta_default
};


const char *batch_lock_path_query[] = {
   /* Mysql */
   "LOCK TABLES Path write, batch write, Path as p write",

   /* Postgresql */
   "BEGIN; LOCK TABLE Path IN SHARE ROW EXCLUSIVE MODE",

   /* SQLite3 */
   "BEGIN"
};

const char *batch_lock_filename_query[] = {
   /* Mysql */
   "LOCK TABLES Filename write, batch write, Filename as f write",

   /* Postgresql */
   "BEGIN; LOCK TABLE Filename IN SHARE ROW EXCLUSIVE MODE",

   /* SQLite3 */
   "BEGIN"
};

const char *batch_unlock_tables_query[] = {
   /* Mysql */
   "UNLOCK TABLES",

   /* Postgresql */
   "COMMIT",

   /* SQLite3 */
   "COMMIT"
};

const char *batch_fill_path_query[] = {
   /* Mysql */
   "INSERT INTO Path (Path) "
      "SELECT a.Path FROM "
         "(SELECT DISTINCT Path FROM batch) AS a WHERE NOT EXISTS "
         "(SELECT Path FROM Path AS p WHERE p.Path = a.Path)",
 
   /* PostgreSQL */
   "INSERT INTO Path (Path)"
      "SELECT a.Path FROM "
         "(SELECT DISTINCT Path FROM batch) AS a "
       "WHERE NOT EXISTS (SELECT Path FROM Path WHERE Path = a.Path) ",
 
   /* SQLite */
   "INSERT INTO Path (Path)"
      "SELECT DISTINCT Path FROM batch "
      "EXCEPT SELECT Path FROM Path"
};

const char *batch_fill_filename_query[] = {
   /* Mysql */
   "INSERT INTO Filename (Name) "
      "SELECT a.Name FROM "
         "(SELECT DISTINCT Name FROM batch) AS a WHERE NOT EXISTS "
         "(SELECT Name FROM Filename AS f WHERE f.Name = a.Name)",

   /* Postgresql */
   "INSERT INTO Filename (Name) "
      "SELECT a.Name FROM "
         "(SELECT DISTINCT Name FROM batch) as a "
       "WHERE NOT EXISTS "
        "(SELECT Name FROM Filename WHERE Name = a.Name)",

   /* SQLite3 */
   "INSERT INTO Filename (Name) "
      "SELECT DISTINCT Name FROM batch "
      "EXCEPT SELECT Name FROM Filename"
};

const char *match_query[] = {
   /* Mysql */
   "REGEXP",
   /* PostgreSQL */
   "~", 
   /* SQLite */
   "LIKE"                       /* MATCH doesn't seems to work anymore... */
}; 
 
static const char *insert_counter_values_default =
   "INSERT INTO Counters (Counter, MinValue, "
   "MaxValue, CurrentValue, WrapCounter) "
   "VALUES ('%s','%d','%d','%d','%s')";

const char *insert_counter_values[] = {
   /* MySQL */
   "INSERT INTO Counters (Counter, Counters.MinValue, "
   "Counters.MaxValue, CurrentValue, WrapCounter) "
   "VALUES ('%s','%d','%d','%d','%s')",

   /* PostgreSQL */
   insert_counter_values_default,

   /* SQLite */
   insert_counter_values_default
};

static const char *select_counter_values_default = 
   "SELECT MinValue, MaxValue, CurrentValue, WrapCounter"
   " FROM Counters WHERE Counter='%s'";
 
const char *select_counter_values[] =
{
   /* MySQL */
   "SELECT Counters.MinValue, Counters.MaxValue, CurrentValue, WrapCounter"
   " FROM Counters WHERE Counter='%s'",
 
   /* PostgreSQL */
   select_counter_values_default, 
 
   /* SQLite */
   select_counter_values_default 
}; 
 
static const char *update_counter_values_default = 
   "UPDATE Counters SET MinValue=%d, MaxValue=%d, CurrentValue=%d,"
    "WrapCounter='%s' WHERE Counter='%s'";
 
const char *update_counter_values[] =
{
   /* MySQL */
   "UPDATE Counters SET Counters.MinValue=%d, Counters.MaxValue=%d,"
     "CurrentValue=%d, WrapCounter='%s' WHERE Counter='%s'",
   /* PostgreSQL */
   update_counter_values_default, 
   /* SQLite */
   update_counter_values_default 
}; 
 
static const char *expired_volumes_defaults =
"SELECT Media.VolumeName  AS volumename,"
       "Media.LastWritten AS lastwritten"
" FROM  Media"
" WHERE VolStatus IN ('Full', 'Used')"
     " AND ( Media.LastWritten +  Media.VolRetention ) < NOW()"
     " %s ";

const char *prune_cache[] = {
   /* MySQL */
   " (Media.LastWritten + Media.CacheRetention) < NOW() ",
   /* PostgreSQL */
   " (Media.LastWritten + (interval '1 second' * Media.CacheRetention)) < NOW() ",
   /* SQLite */
   " ( strftime('%s', Media.LastWritten) + Media.CacheRetention < strftime('%s', datetime('now', 'localtime'))) "
};

const char *expired_volumes[] = {
   /* MySQL */
   expired_volumes_defaults,
   /* PostgreSQL */
   "SELECT Media.VolumeName, Media.LastWritten "
   " FROM  Media "
   " WHERE VolStatus IN ('Full', 'Used') "
     " AND ( Media.LastWritten + (interval '1 second' * Media.VolRetention ) < NOW()) "
     " %s ",
   /* SQLite */
   expired_volumes_defaults
};

const char *expires_in[] = {
   /* MySQL */
   "(GREATEST(0, CAST(UNIX_TIMESTAMP(LastWritten) + Media.VolRetention AS SIGNED) - UNIX_TIMESTAMP(NOW())))",
   /* PostgreSQL */
   "GREATEST(0, (extract('epoch' from LastWritten + Media.VolRetention * interval '1second' - NOW())::bigint))",
   /* SQLite */
   "MAX(0, (strftime('%s', LastWritten) + Media.VolRetention - strftime('%s', datetime('now', 'localtime'))))"
};

const char *strip_restore[] = {
   /* MySQL */
   "DELETE FROM %s WHERE FileId IN (SELECT * FROM (SELECT FileId FROM %s as B JOIN File USING (FileId) WHERE PathId IN (%s)) AS C)",
   /* PostgreSQL */
   "DELETE FROM %s WHERE FileId IN (SELECT FileId FROM %s JOIN File USING (FileId) WHERE PathId IN (%s))",
   /* SQLite */
   "DELETE FROM %s WHERE FileId IN (SELECT FileId FROM %s JOIN File USING (FileId) WHERE PathId IN (%s))"
};

static const char *escape_char_value_default = "\\";

const char *escape_char_value[] = {
   /* MySQL */
   "\\\\",
   /* PostgreSQL */
   escape_char_value_default,
   /* SQLite */
   escape_char_value_default
};