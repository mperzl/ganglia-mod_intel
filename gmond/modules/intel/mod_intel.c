/******************************************************************************
 *
 *  This module implements additional useful extensions like:
 *    - fwversion
 *    - oslevel
 *    - serial_num
 *    - disk_iops
 *    - disk_read
 *    - disk_write
 *    - cpu_used
 *
 *  The code has been tested with on the following systems (all x86-based):
 *    - SLES 9, SLES 10, SLES 11 and SLES 12
 *    - RHEL 4, RHEL 5, RHEL 6 and RHEL 7
 *    - openSUSE 13.1-42.3
 *    - Fedora Core 20-26
 *    - Debian 7-9
 *    - Ubuntu 14.04-17.10
 *
 *  Written by Michael Perzl (michael@perzl.org)
 *
 *  Version 1.0, Oct 30, 2017
 *
 *  Version 1.0:  Oct 30, 2017
 *                - initial release
 *
 ******************************************************************************/

/*
 * The ganglia metric "C" interface, required for building DSO modules.
 */

#include <gm_metric.h>


#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "gm_file.h"
#include "libmetrics.h"


#define MY_LINE_MAX 4096
#define MY_BUFFSIZE 65536


typedef struct
{
   uint32_t last_read;
   uint32_t thresh;
   char name[256];
   char buffer[MY_BUFFSIZE+MY_LINE_MAX];
} my_timely_file;


static my_timely_file proc_diskstats = { 0, 1, "/proc/diskstats", {0} };
static my_timely_file proc_stat = { 0, 1, "/proc/stat", {0} };

static time_t boottime = 0;



int
my_slurpfile (char *filename, char *buffer, int buflen)
{
   FILE *f;
   int   bytesRead;
   char *p,
        *q;


   f = fopen( filename, "r" );

   if (f == NULL)
   {
      err_ret( "my_slurpfile() open() error on file %s", filename );
      return( SLURP_FAILURE );
   }

   p = buffer;
   bytesRead = 0;

   q = fgets( p, MY_LINE_MAX, f );
   while ((q != NULL) && (p-buffer < buflen))
   {
      p += strlen( q );
      q = fgets( p, MY_LINE_MAX, f );
   }
   
   fclose( f );

   bytesRead = p - buffer;

   if (bytesRead == 0)
   {
      err_ret( "my_slurpfile() read() error on file %s", filename );
      return( SLURP_FAILURE );
   }

   if (bytesRead >= buflen)
   {
      err_ret( "my_slurpfile() read() buffer overflow on file %s", filename );
      return( SLURP_FAILURE );
   }

   return( bytesRead );
}



static char *
my_update_file( my_timely_file *tf )
{
   time_t now;
   int    rval; 


   now = time( NULL );
   rval = 0;
   if (now - tf->last_read > tf->thresh)
   {
      rval = my_slurpfile( tf->name, tf->buffer, MY_BUFFSIZE );
      if (rval == SYNAPSE_FAILURE)
      {
         err_msg( "my_update_file() got an error from slurpfile() reading %s",
	          tf->name );
         return( (char *) SYNAPSE_FAILURE );
      }
      else
         tf->last_read = now;
   }

   return( tf->buffer );
}



static time_t
boottime_func_CALLED_ONCE( void )
{
   char   *p;
   time_t  boottime;


   p = my_update_file( &proc_stat ); 

   p = strstr( p, "btime" );
   if (p)
   { 
      p = skip_token( p );
      boottime = strtod( p, (char **) NULL );
   }
   else
      boottime = 0;

   return( boottime );
}


g_val_t
cpu_entitlement_func( void )
{
   g_val_t val;


   val = cpu_num_func();

   val.f = 1.0 * val.uint16;

   return( val );
}



g_val_t
cpu_in_lpar_func( void )
{
   g_val_t val;


   val = cpu_num_func();

   val.int32 = val.uint16;

   return( val );
}



g_val_t
cpu_used_func( void )
{
   g_val_t val;
   int     cpus;


/* find out number of CPUs in the system/VM */
   val = cpu_num_func();
   cpus = val.uint16;

/* treat everything except idle as "workload" */
   val = cpu_idle_func();
   val.f = (float) cpus * (100.0 - val.f) / 100.0;

/* sanity check to prevent against accidental huge value */
   if (val.f >= 256.0)
      val.f = 0.0;


   return( val );
}



struct dsk_stat {
        char          dk_name[32];
        int           dk_major;
        int           dk_minor;
        long          dk_noinfo;
        unsigned long dk_reads;
        unsigned long dk_rmerge;
        unsigned long dk_rmsec;
        unsigned long dk_rkb;
        unsigned long dk_writes;
        unsigned long dk_wmerge;
        unsigned long dk_wmsec;
        unsigned long dk_wkb;
        unsigned long dk_xfers;
        unsigned long dk_bsize;
        unsigned long dk_time;
        unsigned long dk_inflight;
        unsigned long dk_11;
        unsigned long dk_partition;
        unsigned long dk_blocks; /* in /proc/partitions only */
        unsigned long dk_use;
        unsigned long dk_aveq;
};



static void
get_diskstats_iops( double *iops )
{
   char *p, *q;
   char buf[1024];
   int  ret;
   long long total_iops, diff;
   static long long saved_iops = 0LL;
   struct dsk_stat dk;
   static double last_time = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);


   p = my_update_file( &proc_diskstats );

   if (p)
   {
      total_iops = 0LL;

      while ((q = strchr( p, '\n' )))
      {
         /* zero the data ready for reading */
         dk.dk_reads = dk.dk_writes = 0;

         strncpy( buf, p, q-p );
         buf[q-p] = '\0';

         ret = sscanf( buf, "%d %d %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &dk.dk_major,
                       &dk.dk_minor,
                       &dk.dk_name[0],
                       &dk.dk_reads,
                       &dk.dk_rmerge,
                       &dk.dk_rkb,
                       &dk.dk_rmsec,
                       &dk.dk_writes,
                       &dk.dk_wmerge,
                       &dk.dk_wkb,
                       &dk.dk_wmsec,
                       &dk.dk_inflight,
                       &dk.dk_time,
                       &dk.dk_11 );

         p = q+1;

         if (ret == 7)  /* skip partitions of a disk */
            continue;

         if (strncmp(dk.dk_name, "dm-", 3) == 0)
            continue;

         if (strncmp(dk.dk_name, "md", 2) == 0)
            continue;

#ifdef MPERZL_DEBUG_EXTRA
fprintf(stderr, "dk_name = %5s, dk_reads = %10ld, dk_writes = %10ld\n", dk.dk_name, dk.dk_reads, dk.dk_writes);
#endif

         total_iops += dk.dk_reads + dk.dk_writes;
      }
#ifdef MPERZL_DEBUG
fprintf(stderr, "total_iops = %" PRIi64 "\n", total_iops);
fprintf(stderr, "saved_iops = %" PRIi64 "\n", saved_iops);
#endif

      delta_t = now - last_time;
#ifdef MPERZL_DEBUG
fprintf(stderr, "delta_t = %g\n", delta_t);
#endif

      if (delta_t > 0)
      {
         diff = total_iops - saved_iops;
#ifdef MPERZL_DEBUG
fprintf(stderr, "diff = %lld\n", diff);
#endif

         if (diff > 0LL)
            *iops = (double) diff / delta_t;
         else
            *iops = 0.0;
      }
      else
         *iops = 0.0;

      saved_iops = total_iops;
   }
   else
   {
      *iops = 0.0;
   }
#ifdef MPERZL_DEBUG
fprintf(stderr, "*iops = %g\n", *iops);
#endif

   last_time = now;
}



static void
get_diskstats_read( double *read )
{
   char *p, *q;
   char buf[1024];
   int  ret;
   long long total_read, diff;
   static long long saved_read = 0LL;
   struct dsk_stat dk;
   static double last_time = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);


   p = my_update_file( &proc_diskstats );

   if (p)
   {
      total_read  = 0;

      while ((q = strchr( p, '\n' )))
      {
         /* zero the data ready for reading */
         dk.dk_rkb = 0;

         strncpy( buf, p, q-p );
         buf[q-p] = '\0';

         ret = sscanf( buf, "%d %d %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &dk.dk_major,
                       &dk.dk_minor,
                       &dk.dk_name[0],
                       &dk.dk_reads,
                       &dk.dk_rmerge,
                       &dk.dk_rkb,
                       &dk.dk_rmsec,
                       &dk.dk_writes,
                       &dk.dk_wmerge,
                       &dk.dk_wkb,
                       &dk.dk_wmsec,
                       &dk.dk_inflight,
                       &dk.dk_time,
                       &dk.dk_11 );

         p = q+1;

         if (ret == 7)  /* skip partitions of a disk */
            continue;

         if (strncmp(dk.dk_name, "md", 2) == 0)
            continue;

         if (strncmp(dk.dk_name, "dm-", 3) == 0)
            continue;

#ifdef MPERZL_DEBUG_EXTRA
printf("dk_rkb = %ld   dk_wkb = %ld\n", dk.dk_rkb, dk.dk_wkb);
#endif

         dk.dk_rkb /= 2; /* sectors = 512 bytes */

         total_read  += dk.dk_rkb;
      }
#ifdef MPERZL_DEBUG
printf("total_read  = %" PRIi64 "\n", total_read);
printf("saved_read  = %" PRIi64 "\n", saved_read);
#endif

      delta_t = now - last_time;
#ifdef MPERZL_DEBUG
fprintf(stderr, "delta_t = %g\n", delta_t);
#endif

      if (delta_t > 0)
      {
         diff = total_read - saved_read;
#ifdef MPERZL_DEBUG
fprintf(stderr, "delta_t = %g\n", delta_t);
#endif

         if (diff > 0LL)
            *read = diff / delta_t;
         else
            *read = 0.0;
      }
      else
         *read = 0.0;

      saved_read  = total_read;
   }
   else
   {
      *read  = 0.0;
   }
#ifdef MPERZL_DEBUG
fprintf(stderr, "*read = %g\n", *read);
#endif

   last_time = now;
}



static void
get_diskstats_write( double *write )
{
   char *p, *q;
   char buf[1024];
   int  ret;
   long long total_write, diff;
   static long long saved_write = 0LL;
   struct dsk_stat dk;
   static double last_time = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);


   p = my_update_file( &proc_diskstats );

   if (p)
   {
      total_write = 0;

      while ((q = strchr( p, '\n' )))
      {
         /* zero the data ready for reading */
         dk.dk_wkb = 0;

         strncpy( buf, p, q-p );
         buf[q-p] = '\0';

         ret = sscanf( buf, "%d %d %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &dk.dk_major,
                       &dk.dk_minor,
                       &dk.dk_name[0],
                       &dk.dk_reads,
                       &dk.dk_rmerge,
                       &dk.dk_rkb,
                       &dk.dk_rmsec,
                       &dk.dk_writes,
                       &dk.dk_wmerge,
                       &dk.dk_wkb,
                       &dk.dk_wmsec,
                       &dk.dk_inflight,
                       &dk.dk_time,
                       &dk.dk_11 );

         p = q+1;

         if (ret == 7)  /* skip partitions of a disk */
            continue;

         if (strncmp(dk.dk_name, "md", 2) == 0)
            continue;

         if (strncmp(dk.dk_name, "dm-", 3) == 0)
            continue;

#ifdef MPERZL_DEBUG_EXTRA
printf("dk_rkb = %ld   dk_wkb = %ld\n", dk.dk_rkb, dk.dk_wkb);
#endif

         dk.dk_wkb /= 2; /* sectors = 512 bytes */

         total_write += dk.dk_wkb;
      }
#ifdef MPERZL_DEBUG
printf("total_write = %" PRIi64 "\n", total_write);
printf("saved_write = %" PRIi64 "\n", saved_write);
#endif

      delta_t = now - last_time;
#ifdef MPERZL_DEBUG
fprintf(stderr, "delta_t = %g\n", delta_t);
#endif

      if (delta_t > 0)
      {
         diff = total_write - saved_write;
#ifdef MPERZL_DEBUG
fprintf(stderr, "delta_t = %g\n", delta_t);
#endif

         if (diff > 0LL)
            *write = diff / delta_t;
         else
            *write = 0.0;
      }
      else
         *write = 0.0;

      saved_write = total_write;
   }
   else
   {
      *write = 0.0;
   }

   last_time = now;
#ifdef MPERZL_DEBUG
fprintf(stderr, "*write = %g\n", *write);
#endif
}



g_val_t
disk_iops_func( void )
{
   g_val_t val;
   double disk_iops;

 
   get_diskstats_iops( &disk_iops );

   val.d = disk_iops;

   return( val );
}



g_val_t
disk_read_func( void )
{
   g_val_t val;
   double disk_read;

 
   get_diskstats_read( &disk_read );

   val.d = disk_read * 1024.0;

   return( val );
}



g_val_t
disk_write_func( void )
{
   g_val_t val;
   double disk_write;

 
   get_diskstats_write( &disk_write );

   val.d = disk_write * 1024.0;

   return( val );
}



g_val_t
fwversion_func( void )
{
   FILE    *f;
   g_val_t val;
   char buf1[128], buf2[128];
   int l1, l2;


   f = popen( "/usr/sbin/dmidecode -s bios-vendor | grep -v '^#' 2>/dev/null", "r" );

   if (f == NULL)
      strcpy( val.str, "popen() of 'dmidecode' failed" );
   else
   {
      memset( buf1, '\0', 128 );
      if (fgets( buf1, 128, f ) != NULL)
      {
         buf1[127] = '\0';
         buf1[strlen( buf1 ) - 1] = ' ';  /* replace \n */
      }
      else
         strcpy( buf1, "No output of 'dmidecode'" );

      pclose( f );

      f = popen( "/usr/sbin/dmidecode -s bios-version | grep -v '^#' 2>/dev/null", "r" );

      if (f == NULL)
         strcpy( val.str, "popen() of 'dmidecode' failed" );
      else
      {
         memset( buf2, '\0', 128 );
         if (fgets( buf2, 128, f ) != NULL)
         {
            buf2[127] = '\0';
            buf2[strlen( buf2 ) - 1] = '\0';  /* remove \n */
         }
         else
            strcpy( buf2, "No output of 'dmidecode'" );

         l1 = strlen( buf1 );
         if (l1 < MAX_G_STRING_SIZE)
         {
            strcpy( val.str, buf1 );

            l2 = strlen( buf2 );
            if (l1 + l2 < MAX_G_STRING_SIZE)
               strcat( val.str, buf2 );
            else
               strncat( val.str, buf2, MAX_G_STRING_SIZE-l1-1 );
         }
         else
            strncat( val.str, buf1, MAX_G_STRING_SIZE-1 );
      }

      pclose( f );
   }

   return( val );
}



static int LinuxVersion = 0;    /* 1 = /etc/SuSE-release */
                                /* 2 = /etc/redhat-release */
                                /* 3 = /etc/os-release */
                                /* 4 = /etc/debian_version */



/* find 64bit kernel or not */
g_val_t
kernel64bit_func( void )
{
   g_val_t  val;
   FILE    *f;
   char     buf[128];
   int      kernel64bit, i;


   if ((LinuxVersion == 1) || (LinuxVersion == 2))
      f = popen( "uname -i 2>/dev/null", "r" );
   else
      if ((LinuxVersion == 3) || (LinuxVersion == 4))  /* Debian has no "uname -i" */
         f = popen( "uname -m 2>/dev/null", "r" );
      else
         f = popen( "uname -r 2>/dev/null", "r" );

   if (f == NULL)
      strcpy( val.str, "popen() of 'uname -[i,m,r]' failed" );
   else
   {
      if (fread( buf, 1, 128, f ) > 0)
      {
         kernel64bit = FALSE;
         buf[127] = '\0';

         for (i = 0;  buf[i] != '\0';  i++)
         {
            if (buf[i] == '6' && buf[i+1] == '4')
            {
               kernel64bit = TRUE;
               break;
            }
         }

         strcpy( val.str, kernel64bit ? "yes" : "no" );
      }
      else
         strcpy( val.str, "popen() of 'uname -[i,m,r]' failed" );

      pclose( f );
   }

   return( val );
}



/* find OS version just once */
static g_val_t
oslevel_func_CALLED_ONCE( void )
{
   g_val_t  val;
   FILE    *f, *f2;
   char     buf[256], *p, *q;
   int      i;


   f = fopen( "/etc/SuSE-release", "r" );
   if (f) LinuxVersion = 1;
   if (! LinuxVersion)
   {
      f = fopen( "/etc/redhat-release", "r" );
      if (f) LinuxVersion = 2;
   }
   if (! LinuxVersion)
   {
      f = popen( "cat /etc/os-release 2>/dev/null", "r" );
      if (f) LinuxVersion = 3;
   }
   if (! LinuxVersion)
   {
      f = fopen( "/etc/debian_version", "r" );
      if (f) LinuxVersion = 4;
   }
   if (f == NULL)
      strcpy( val.str, "No known Linux release found" );
   else
   {
      if (fread( buf, 1, 256, f) > 0)
      {
         if (LinuxVersion == 1)
         {
            if ((! strncmp( buf, "SUSE LINUX Enterprise Server", 28 )) ||
                (! strncmp( buf, "SUSE Linux Enterprise Server", 28 )))
            {
               strcpy( val.str, "SLES " );

               p = strchr( buf, '\n' );
               if (p) p = strchr( p+1, '=' );
               if (p) p = skip_whitespace( p+1 );
               if (p) q = strchr( p, '\n' );

               if (p && q) strncat( val.str, p, q-p );
               strcat( val.str, " SP " );

               if (q) p = strchr( q+1, '=' );
               if (p) p = skip_whitespace( p+1 );
               if (p) q = strchr( p, '\n' );

               if (p && q) strncat( val.str, p, q-p );
            }
            else
            {
               p = strchr( buf, '\n' );

               i = p-buf;
               if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                  i = MAX_G_STRING_SIZE - 1;

               strncpy( val.str, buf, i );
               val.str[i] = '\0';
            }
         }
         else if (LinuxVersion == 2)
         {
            if (! strncmp( buf, "Red Hat Enterprise Linux AS release", 35 ))
            {
               strcpy( val.str, "Red Hat Enterprise Linux " );

               p = skip_whitespace( buf+35 );
               if (p) q = strchr( p, ' ' );

               if (p && q) strncat( val.str, p, q-p );

               if (q) p = strstr( q+1, "Update " );

               if (p)
               {
                  strcat( val.str, " Update " );

                  p = skip_whitespace( p+7 );
                  if (p) q = strchr( p, ')' );

                  if (p && q) strncat( val.str, p, q-p );
               }
            }
            else
            if (! strncmp( buf, "Red Hat Enterprise Linux Server release", 39 ))
            {
               strcpy( val.str, "Red Hat Enterprise Linux " );

               p = skip_whitespace( buf+39 );
               if (p) q = strchr( p, ' ' );

               if (p && q) strncat( val.str, p, q-p );
            }
            else
            {
               p = strchr( buf, '\n' );

               i = p-buf;
               if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                  i = MAX_G_STRING_SIZE - 1;

               strncpy( val.str, buf, i );
               val.str[i] = '\0';
            }
         }
         else if (LinuxVersion == 3)
         {
            f2 = popen( "cat /etc/os-release | egrep '^NAME=|^VERSION=' | sed 's/NAME=//g' | sed 's/\"//g' | sed 's/VERSION=//g' 2>/dev/null", "r" );

            if (f2)
            {
               if (fread( buf, 1, 256, f2) > 0)
               {
                  p = strchr( buf, '\n' );
                  i = p-buf;
                  if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                     i = MAX_G_STRING_SIZE - 1;
                  buf[i] = ' ';

                  p = strchr( p+1, '\n' );
                  i = p-buf;
                  if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                     i = MAX_G_STRING_SIZE - 1;

                  strncpy( val.str, buf, i );
                  val.str[i] = '\0';
                  
                  pclose( f2 );
               }
            }
            else
               strcpy( val.str, "Couldn't read /etc/os-release" );
         }
         else if (LinuxVersion == 4)
         {
            p = strchr( buf, '\n' );

            i = p-buf;
            if ((i < 0) || (i >= MAX_G_STRING_SIZE))
               i = MAX_G_STRING_SIZE - 1;

            strncpy( val.str, buf, i );
            val.str[i] = '\0';
         }
      }
      else
         strcpy( val.str, "No known Linux release found" );

      fclose( f );
   }

   return( val );
}



g_val_t
oslevel_func( void )
{
   static g_val_t val;
   static int firstTime = TRUE;


   if (firstTime)
   {
      val = oslevel_func_CALLED_ONCE();
      firstTime = FALSE;
   }

   return( val );
}



g_val_t
model_name_func( void )
{
   FILE    *f;
   g_val_t  val;


   strcpy( val.str, "" );

   f = popen( "/usr/sbin/dmidecode -s system-product-name | grep -v '^#' 2>/dev/null", "r" );

   if (f == NULL)
      strcpy( val.str, "popen() of 'dmidecode' failed" );
   else
   {
      if (fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL)
      {
         val.str[MAX_G_STRING_SIZE - 1] = '\0';
         val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
      }
      else
         strcpy( val.str, "No output of 'dmidecode'" );

      pclose( f );
   }

   return( val );
}



g_val_t
serial_num_func( void )
{
   FILE    *f;
   g_val_t  val;


   strcpy( val.str, "" );

   f = popen( "/usr/sbin/dmidecode -s system-serial-number | grep -v '^#' 2>/dev/null", "r" );

   if (f == NULL)
      strcpy( val.str, "popen() of 'dmidecode' failed" );
   else
   {
      if (fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL)
      {
         val.str[MAX_G_STRING_SIZE - 1] = '\0';
         val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
      }
      else
         strcpy( val.str, "No output of 'dmidecode'" );

      pclose( f );
   }

   return( val );
}


g_val_t
kvm_guest_func( void )
{
   FILE    *f;
   char     buf[128];
   g_val_t  val;


   strcpy( val.str, "no" );

   f = popen( "/usr/sbin/dmidecode -s system-product-name | grep -v '^#' 2>/dev/null", "r" );

   if (f == NULL)
      strcpy( val.str, "popen() of 'dmidecode' failed" );
   else
   {
      if (fgets( buf, MAX_G_STRING_SIZE, f ) != NULL)
      {
         buf[MAX_G_STRING_SIZE - 1] = '\0';
         buf[strlen( buf ) - 1] = '\0';  /* truncate \n */
      }
      else
         strcpy( val.str, "No output of 'dmidecode'" );

      pclose( f );

      if (! strcmp( buf, "KVM" ))
         strcpy( val.str, "yes" );
   }

   return( val );
}



g_val_t
cpu_type_func( void )
{
   FILE    *f;
   g_val_t  val;
   char     buf[256],
           *p, *q;
   int      l;


   strcpy( val.str, "Unknown" );

   f = popen( "cat /proc/cpuinfo | grep 'model name'", "r" );

   if (f == NULL)
      strcpy( val.str, "'cat /proc/cpuinfo' failed" );
   else
   {
      memset( buf, '\0', 256 );
      if (fgets( buf, 256, f ) > 0)
      {
         p = strchr( buf, ':' );
         if (p) p = skip_whitespace( p+1 );
         if (p) q = strchr( p, '\n' );

         if (p && q)
         {
            l = q - p;
            if (l > MAX_G_STRING_SIZE - 1)
               l = MAX_G_STRING_SIZE - 1;

            strncpy( val.str, p, l );
            val.str[l] = '\0';
         }
      }
      else
         strcpy( val.str, "No output from 'proc/cpuinfo'" );

      pclose( f );
   }

   return( val );
}



/*
 * Declare ourselves so the configuration routines can find and know us.
 * We'll fill it in at the end of the module.
 */
extern mmodule intel_module;


static int
intel_metric_init ( apr_pool_t *p )
{
   int i;
   g_val_t val;


   for (i = 0;  intel_module.metrics_info[i].name != NULL;  i++)
   {
      /* Initialize the metadata storage for each of the metrics and then
       *  store one or more key/value pairs.  The define MGROUPS defines
       *  the key for the grouping attribute. */
      MMETRIC_INIT_METADATA( &(intel_module.metrics_info[i]), p );
      MMETRIC_ADD_METADATA( &(intel_module.metrics_info[i]), MGROUP, "intel" );
   }


/* initialize the routines which require a time interval */

   boottime = boottime_func_CALLED_ONCE();

   val = oslevel_func();
   val = cpu_used_func();
   val = disk_iops_func();
   val = disk_read_func();
   val = disk_write_func();


/* return SUCCESS */

   return( 0 );
}



static void
intel_metric_cleanup ( void )
{
}



static g_val_t
intel_metric_handler ( int metric_index )
{
   g_val_t val;

/* The metric_index corresponds to the order in which
   the metrics appear in the metric_info array
*/
   switch (metric_index)
   {
      case 0:  return( cpu_entitlement_func() );
      case 1:  return( cpu_in_lpar_func() );
      case 2:  return( cpu_used_func() );
      case 3:  return( disk_iops_func() );
      case 4:  return( disk_read_func() );
      case 5:  return( disk_write_func() );
      case 6:  return( fwversion_func() );
      case 7:  return( kernel64bit_func() );
      case 8:  return( oslevel_func() );
      case 9:  return( model_name_func() );
      case 10: return( serial_num_func() );
      case 11: return( kvm_guest_func() );
      case 12: return( cpu_type_func() );
      default: val.uint32 = 0; /* default fallback */
   }

   return( val );
}



static Ganglia_25metric intel_metric_info[] = 
{
   {0, "cpu_entitlement",  180, GANGLIA_VALUE_FLOAT,        "CPUs", "both", "%.2f", UDP_HEADER_SIZE+8,  "Capacity entitlement in units of physical cores"},
   {0, "cpu_in_lpar",      180, GANGLIA_VALUE_UNSIGNED_INT, "CPUs", "both", "%d",   UDP_HEADER_SIZE+8,  "Number of CPUs the OS sees in the system"},
   {0, "cpu_used",          15, GANGLIA_VALUE_FLOAT,        "CPUs", "both", "%.4f", UDP_HEADER_SIZE+8,  "Number of physical cores used"},
   {0, "disk_iops",        180, GANGLIA_VALUE_DOUBLE,     "IO/sec", "both", "%.3f", UDP_HEADER_SIZE+16, "Total number of I/O operations per second"},
   {0, "disk_read",        180, GANGLIA_VALUE_DOUBLE,  "bytes/sec", "both", "%.2f", UDP_HEADER_SIZE+16, "Total number of bytes read I/O of the system"},
   {0, "disk_write",       180, GANGLIA_VALUE_DOUBLE,  "bytes/sec", "both", "%.2f", UDP_HEADER_SIZE+16, "Total number of bytes write I/O of the system"},
   {0, "fwversion",       1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+MAX_G_STRING_SIZE, "Firmware Version"},
   {0, "kernel64bit",     1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+MAX_G_STRING_SIZE, "Is the kernel running in 64-bit mode?"},
   {0, "oslevel",          180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+MAX_G_STRING_SIZE, "Exact Linux version string"},
   {0, "model_name",      1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+MAX_G_STRING_SIZE, "Machine Model Name"},
   {0, "serial_num",      1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+MAX_G_STRING_SIZE, "Serial number of the hardware system"},
   {0, "kvm_guest",       1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+MAX_G_STRING_SIZE, "Is this a KVM guest VM or not?"},
   {0, "cpu_type",         180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+MAX_G_STRING_SIZE, "CPU model name"},
   {0, NULL}
};



mmodule intel_module =
{
   STD_MMODULE_STUFF,
   intel_metric_init,
   intel_metric_cleanup,
   intel_metric_info,
   intel_metric_handler,
};

