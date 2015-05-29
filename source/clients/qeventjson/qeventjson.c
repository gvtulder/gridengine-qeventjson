/*___INFO__MARK_BEGIN__*/
/*************************************************************************
 *
 *  This modified version of qevent subscribes to job and task events
 *  and prints these events to STDOUT as JSON objects.
 *
 *  Gijs van Tulder, g.vantulder@erasmusmc.nl
 *  Biomedical Imaging Group Rotterdam, Erasmus MC, the Netherlands
 *  May 2015
 *
 ************************************************************************/
/*************************************************************************
 * 
 *  The Contents of this file are made available subject to the terms of
 *  the Sun Industry Standards Source License Version 1.2
 * 
 *  Sun Microsystems Inc., March, 2001
 * 
 * 
 *  Sun Industry Standards Source License Version 1.2
 *  =================================================
 *  The contents of this file are subject to the Sun Industry Standards
 *  Source License Version 1.2 (the "License"); You may not use this file
 *  except in compliance with the License. You may obtain a copy of the
 *  License at http://gridengine.sunsource.net/Gridengine_SISSL_license.html
 * 
 *  Software provided under this License is provided on an "AS IS" basis,
 *  WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
 *  WITHOUT LIMITATION, WARRANTIES THAT THE SOFTWARE IS FREE OF DEFECTS,
 *  MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE, OR NON-INFRINGING.
 *  See the License for the specific provisions governing your rights and
 *  obligations concerning the Software.
 * 
 *   The Initial Developer of the Original Code is: Sun Microsystems, Inc.
 * 
 *   Copyright: 2001 by Sun Microsystems, Inc.
 * 
 *   All Rights Reserved.
 * 
 ************************************************************************/
/*___INFO__MARK_END__*/
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#if defined(FREEBSD) || defined(NETBSD) || defined(DARWIN)
#include <sys/time.h>
#endif

#include "sge_string.h"
#include "sge_unistd.h"
#include "sge_all_listsL.h"
#include "usage.h"
#include "sig_handlers.h"
#include "commlib.h"
#include "sge_prog.h"
#include "sgermon.h"
#include "sge_log.h"

#include "msg_clients_common.h"
#include "msg_common.h"

#include "sge_answer.h"
#include "sge_mirror.h"
#include "sge_event.h"
#include "sge_time.h"
#include "sge_feature.h"
#include "sge_spool.h"
#include "qeventjson.h"
#include "sge_profiling.h"
#include "sge_mt_init.h"
#include "sgeobj/sge_job.h"
#include "sgeobj/sge_range.h"

#include "gdi/sge_gdi_ctx.h"

#include "frozen.h"

static void qeventjson_subscribe_mode(sge_evc_class_t *evc);

static void
make_json_resource_list(char *json_buffer, lListElem *rp) {
  const char *key, *val;
  json_buffer[0] = '{';
  json_buffer++;
  while (rp) {
    key = lGetString(rp, CE_name);
    val = lGetString(rp, CE_stringval);
    json_buffer += json_emit_quoted_str(json_buffer, 50, key, strlen(key));
    json_buffer[0] = ':';
    json_buffer++;
    json_buffer += json_emit_quoted_str(json_buffer, 50, val, strlen(val));
    rp = lNext(rp);
    if (rp) {
      json_buffer[0] = ',';
      json_buffer++;
    }
  }
  json_buffer[0] = '}';
  json_buffer[1] = '\0';
}

static void
make_json_queue_list(char *json_buffer, lListElem *qp) {
  const char *queueName;
  int b;
  json_buffer[0] = '[';
  json_buffer++;
  while (qp) {
    queueName = lGetString(qp, QR_name);
    b = json_emit_quoted_str(json_buffer, 50, queueName, strlen(queueName));
    json_buffer += b;
    qp = lNext(qp);
    if (qp) {
      json_buffer[0] = ',';
      json_buffer++;
    }
  }
  json_buffer[0] = ']';
  json_buffer[1] = '\0';
}

static void
print_json(const char *format, ...) {
   // print json
   char json_buffer[1024*12];
   va_list argptr;
   va_start(argptr, format);
   int jsonlen = json_emit_va(json_buffer, sizeof(json_buffer),
                              format, argptr);
   va_end(argptr);
   if (jsonlen < sizeof(json_buffer)) {
     fprintf(stdout, "%s\n", json_buffer);
   }
   fflush(stdout);
}

static void print_jobs_not_enrolled(u_long32 event_type, u_long32 timestamp, u_long job_id, lListElem *job, const char *job_name, const char *owner, const char *queues_json, const char *resources_json, u_long n_slots, u_long submission_time) {
   lList *range_list[16];         /* RN_Type */
   u_long32 hold_state[16];
   int i;

   job_create_hold_id_lists(job, range_list, hold_state); 
   for (i = 0; i <= 15; i++) {
      if (range_list[i] != NULL) { 
         lListElem *range; /* RN_Type */
         
         for_each(range, range_list[i]) {
            u_long32 start, end, step;
            range_get_all_ids(range, &start, &end, &step);
            for (; start <= end; start += step) { 
               // create from template
               lListElem *jatask = job_get_ja_task_template_hold(job,
                                                       start, hold_state[i]);
               
               // priority
               double prio = lGetDouble(jatask, JAT_prio);
               // status string
               u_long32 jstate = lGetUlong(jatask, JAT_status) | lGetUlong(jatask, JAT_hold) | lGetUlong(jatask, JAT_state);
               if (lGetList(job, JB_jid_predecessor_list) || lGetUlong(jatask, JAT_hold)) {
                  jstate |= JHELD;
               }
               char status_str[100];
               job_get_state_string(status_str, jstate);

               // print json
               print_json("{s:s,s:u,s:i,s:i,s:s,s:s,s:S,s:S,s:u,s:u,s:f,s:s,s:u}",
                          "event", (event_type==sgeE_JOB_ADD?"JOB_ADD":(event_type==sgeE_JOB_MOD?"JOB_MOD":"JOB_LIST")),
                          "timestamp", timestamp,
                          "job_id", job_id,
                          "task_id", (u_long) start,
                          "job_name", job_name,
                          "owner", owner,
                          "queues", queues_json,
                          "resources", resources_json,
                          "nslots", n_slots,
                          "submission_time", submission_time,
                          "prio", prio,
                          "state", status_str,
                          "start_time", (u_long)0);
            }
         }
      }
   }

   job_destroy_hold_id_lists(job, range_list); 
}                 

static sge_callback_result 
print_event(sge_evc_class_t *evc, object_description *object_base, sge_object_type type, 
            sge_event_action action, lListElem *event, void *clientdata)
{
   char buffer[1024];
   dstring buffer_wrapper;

   sge_dstring_init(&buffer_wrapper, buffer, sizeof(buffer));

   fprintf(stdout, "# %s\n", event_text(event, &buffer_wrapper));
   fflush(stdout);
   /* create a callback error to test error handling */
   if(type == SGE_TYPE_GLOBAL_CONFIG) {
      return SGE_EMA_FAILURE;
   }

   u_long32 event_type = lGetUlong(event, ET_type);
   u_long32 timestamp = lGetUlong(event, ET_timestamp);

   if (event_type == sgeE_JOB_LIST) {
     // initial job list
     lList *jobs = lGetList(event,ET_new_version);
     lListElem *job = lFirst(jobs);

     while (job != NULL) {
       u_long job_id = lGetUlong(job, JB_job_number);

       // which queue?
       char queues_json[1000];
       make_json_queue_list(queues_json, lFirst(lGetList(job, JB_hard_queue_list)));

       // name
       const char *job_name = lGetString(job, JB_job_name);

       // owner
       const char *owner = lGetString(job, JB_owner);

       // submission time
       u_long submission_time = lGetUlong(job, JB_submission_time);

       // memory required
       char resources_json[1000];
       make_json_resource_list(resources_json, lFirst(lGetList(job, JB_hard_resource_list)));

       // number of slots
       u_long n_slots = range_list_get_first_id(lGetList(job, JB_pe_range), NULL);

       lList *jatasks = lGetList(job, JB_ja_tasks);
       lListElem *jatask = lFirst(jatasks);
       while (jatask != NULL) {
         u_long task_id = lGetUlong(jatask, JAT_task_number);

         const char *running_queue = NULL;
         if (jatask)
           running_queue = lGetString(jatask, JAT_master_queue);

         // priority
         double prio = lGetDouble(jatask, JAT_prio);
         // status string
         u_long32 jstate = lGetUlong(jatask, JAT_status) | lGetUlong(jatask, JAT_hold) | lGetUlong(jatask, JAT_state);
         if (lGetList(job, JB_jid_predecessor_list) || lGetUlong(jatask, JAT_hold)) {
            jstate |= JHELD;
         }
         // start time
         u_long start_time = lGetUlong(jatask, JAT_start_time);
         char status_str[100];
         job_get_state_string(status_str, jstate);

         if (running_queue == NULL) {
           // print json
           print_json("{s:s,s:u,s:u,s:u,s:s,s:s,s:S,s:S,s:u,s:u,s:u,s:f,s:s}",
                      "event", "JOB_LIST",
                      "timestamp", timestamp,
                      "job_id", job_id,
                      "task_id", task_id,
                      "job_name", job_name,
                      "owner", owner,
                      "queues", queues_json,
                      "resources", resources_json,
                      "nslots", n_slots,
                      "submission_time", submission_time,
                      "start_time", start_time,
                      "prio", prio,
                      "state", status_str);
         } else {
           // print json
           print_json("{s:s,s:u,s:u,s:u,s:s,s:s,s:S,s:S,s:u,s:u,s:u,s:s,s:f,s:s}",
                      "event", "JOB_LIST",
                      "timestamp", timestamp,
                      "job_id", job_id,
                      "task_id", task_id,
                      "job_name", job_name,
                      "owner", owner,
                      "queues", queues_json,
                      "resources", resources_json,
                      "nslots", n_slots,
                      "submission_time", submission_time,
                      "start_time", start_time,
                      "running_queue", running_queue,
                      "prio", prio,
                      "state", status_str);
         }

         jatask = lNext(jatask);
       }

       print_jobs_not_enrolled(event_type, timestamp, job_id, job, job_name, owner, queues_json, resources_json, n_slots, submission_time);

       job = lNext(job);
     }

     print_json("{s:s}", "event", "JOB_LIST_COMPLETE");

   } else if (event_type == sgeE_JOB_ADD ||
       event_type == sgeE_JOB_MOD) {
     // event for job or task
     u_long job_id = lGetUlong(event, ET_intkey);
     u_long task_id = lGetUlong(event, ET_intkey2);

     // get job
     lList *jobs = lGetList(event,ET_new_version);
     lListElem *job = lFirst(jobs);

     // which queue?
     char queues_json[1000];
     make_json_queue_list(queues_json, lFirst(lGetList(job, JB_hard_queue_list)));

     // name
     const char *job_name = lGetString(job, JB_job_name);

     // owner
     const char *owner = lGetString(job, JB_owner);

     // submission time
     u_long submission_time = lGetUlong(job, JB_submission_time);

     // number of slots
     u_long n_slots = range_list_get_first_id(lGetList(job, JB_pe_range), NULL);

     // memory required
     char resources_json[1000];
     make_json_resource_list(resources_json, lFirst(lGetList(job, JB_hard_resource_list)));

     // for a specific task?
     if (task_id == 0) {
       // no specific task
       print_jobs_not_enrolled(event_type, timestamp, job_id, job, job_name, owner, queues_json, resources_json, n_slots, submission_time);

     } else {
       lList *jatasks = lGetList(job, JB_ja_tasks);
       lListElem *jatask = lFirst(jatasks);

       const char *running_queue = NULL;
       if (jatask)
         running_queue = lGetString(jatask, JAT_master_queue);

       if (running_queue == NULL) {
         // print json
         print_json("{s:s,s:u,s:u,s:u,s:s,s:s,s:S,s:S,s:u,s:u}",
                    "event", (event_type==sgeE_JOB_ADD?"JOB_ADD":"JOB_MOD"),
                    "timestamp", timestamp,
                    "job_id", job_id,
                    "task_id", task_id,
                    "job_name", job_name,
                    "owner", owner,
                    "queues", queues_json,
                    "resources", resources_json,
                    "nslots", n_slots,
                    "submission_time", submission_time);
       } else {
         // print json
         print_json("{s:s,s:u,s:u,s:u,s:s,s:s,s:S,s:S,s:s,s:u,s:u}",
                    "event", (event_type==sgeE_JOB_ADD?"JOB_ADD":"JOB_MOD"),
                    "timestamp", timestamp,
                    "job_id", job_id,
                    "task_id", task_id,
                    "job_name", job_name,
                    "owner", owner,
                    "queues", queues_json,
                    "resources", resources_json,
                    "running_queue", running_queue,
                    "nslots", n_slots,
                    "submission_time", submission_time);
       }
     }

   } else if (event_type == sgeE_JOB_DEL) {
     // event for job (or the last task in the job)
     u_long job_id  = lGetUlong(event, ET_intkey);
     u_long task_id  = lGetUlong(event, ET_intkey2);
     
     // print json
     print_json("{s:s,s:u,s:u,s:u}",
                "event", "JOB_DEL",
                "timestamp", timestamp,
                "job_id", job_id,
                "task_id", task_id);

   } else if (event_type == sgeE_JATASK_ADD ||
              event_type == sgeE_JATASK_MOD) {
     // event for task
     u_long job_id  = lGetUlong(event, ET_intkey);
     u_long task_id  = lGetUlong(event, ET_intkey2);

     // get task
     lList *jatasks = lGetList(event,ET_new_version);
     lListElem *jatask = lFirst(jatasks);

     // priority
     double prio = lGetDouble(jatask, JAT_prio);

     // start time
     u_long start_time = lGetUlong(jatask, JAT_start_time);

     // status string
     u_long32 jstate = lGetUlong(jatask, JAT_status) | lGetUlong(jatask, JAT_hold) | lGetUlong(jatask, JAT_state);
     // no job available?
     //   if (lGetList(job, JB_jid_predecessor_list) || lGetUlong(jatask, JAT_hold)) {
     //      jstate |= JHELD;
     //   }
     char status_str[100];
     job_get_state_string(status_str, jstate);

     const char *running_queue;
     running_queue = lGetString(jatask, JAT_master_queue);

     if (running_queue == NULL) {
       // print json
       print_json("{s:s,s:u,s:u,s:u,s:f,s:s,s:u}",
                  "event", (event_type==sgeE_JATASK_ADD?"JATASK_ADD":"JATASK_MOD"),
                  "timestamp", timestamp,
                  "job_id", job_id,
                  "task_id", task_id,
                  "prio", prio,
                  "state", status_str,
                  "start_time", start_time);
     } else {
       // print json
       print_json("{s:s,s:u,s:u,s:u,s:f,s:s,s:s,s:u}",
                  "event", (event_type==sgeE_JATASK_ADD?"JATASK_ADD":"JATASK_MOD"),
                  "timestamp", timestamp,
                  "job_id", job_id,
                  "task_id", task_id,
                  "prio", prio,
                  "state", status_str,
                  "running_queue", running_queue,
                  "start_time", start_time);
     }

   } else if (event_type == sgeE_JATASK_DEL) {
     // event for job (or the last task in the job)
     u_long job_id  = lGetUlong(event, ET_intkey);
     u_long task_id  = lGetUlong(event, ET_intkey2);
     
     // print json
     print_json("{s:s,s:u,s:u,s:u}",
                "event", "JOB_DEL",
                "timestamp", timestamp,
                "job_id", job_id,
                "task_id", task_id);

   } else if (event_type == sgeE_JOB_USAGE || event_type == sgeE_JOB_FINAL_USAGE) {
     // event for job (or the last task in the job)
     u_long job_id = lGetUlong(event, ET_intkey);
     u_long task_id = lGetUlong(event, ET_intkey2);

     u_long vmem = 0;
     u_long maxvmem = 0;
     u_long end_time = 0;
     u_long exit_status = 0;

     lList *uas = lGetList(event, ET_new_version);
     lListElem *ua = lFirst(uas);
     const char *fieldName;
     while (ua) {
       fieldName = lGetString(ua, UA_name);
       if (strcmp("vmem", fieldName) == 0) {
         vmem = (u_long) lGetDouble(ua, UA_value);
       } else if (strcmp("maxvmem", fieldName) == 0) {
         maxvmem = (u_long) lGetDouble(ua, UA_value);
       } else if (strcmp("end_time", fieldName) == 0) {
         end_time = (u_long) lGetDouble(ua, UA_value);
       } else if (strcmp("exit_status", fieldName) == 0) {
         exit_status = (u_long) lGetDouble(ua, UA_value);
       }
       ua = lNext(ua);
     }

     if (event_type == sgeE_JOB_USAGE) {
       print_json("{s:s,s:u,s:u,s:u,s:u,s:u}",
                  "event", "JOB_USAGE",
                  "timestamp", timestamp,
                  "job_id", job_id,
                  "task_id", task_id,
                  "vmem", vmem,
                  "maxvmem", maxvmem);
     } else {
       print_json("{s:s,s:u,s:u,s:u,s:u,s:u,s:u,s:u}",
                  "event", "JOB_FINAL_USAGE",
                  "timestamp", timestamp,
                  "job_id", job_id,
                  "task_id", task_id,
                  "vmem", vmem,
                  "maxvmem", maxvmem,
                  "end_time", end_time,
                  "exit_status", exit_status);
     }
   }
   
   return SGE_EMA_OK;
}

int main(int argc, char *argv[])
{
   int gdi_setup;
   lList *alp = NULL;
   sge_gdi_ctx_class_t *ctx = NULL; 
   sge_evc_class_t *evc = NULL;

   log_state_set_log_gui(1);
   sge_setup_sig_handlers(QEVENT);

   /* setup event client */
   gdi_setup = sge_gdi2_setup(&ctx, QEVENT, MAIN_THREAD, &alp);
   if (gdi_setup != AE_OK) {
      answer_list_output(&alp);
      SGE_EXIT((void**)&ctx, 1);
   }
   /* TODO: how is the memory we allocate here released ???, SGE_EXIT doesn't */
   if (false == sge_gdi2_evc_setup(&evc, ctx, EV_ID_ANY, &alp, NULL)) {
      answer_list_output(&alp);
      SGE_EXIT((void**)&ctx, 1);
   }

   qeventjson_subscribe_mode(evc);
   SGE_EXIT((void**)&ctx, 0);

   return 0;
}

/****** qeventjson/qeventjson_subscribe_mode() *****************************************
*  NAME
*     qeventjson_subscribe_mode() -- ??? 
*
*  SYNOPSIS
*     static void qeventjson_subscribe_mode(sge_evc_class_t *evc) 
*
*  FUNCTION
*     ??? 
*
*  INPUTS
*     sge_evc_class_t *evc - ??? 
*
*  RESULT
*     static void - 
*
*  EXAMPLE
*     ??? 
*
*  NOTES
*     MT-NOTE: qeventjson_subscribe_mode() is not MT safe 
*
*  BUGS
*     ??? 
*
*  SEE ALSO
*     ???/???
*******************************************************************************/
static void qeventjson_subscribe_mode(sge_evc_class_t *evc) 
{
   sge_object_type event_type = SGE_TYPE_ADMINHOST;
   
   sge_mirror_initialize(evc, EV_ID_ANY, "qevent", true,
                         NULL, NULL, NULL, NULL, NULL);
   sge_mirror_subscribe(evc, SGE_TYPE_SHUTDOWN, print_event, NULL, NULL, NULL, NULL);
   sge_mirror_subscribe(evc, SGE_TYPE_ADMINHOST, print_event, NULL, NULL, NULL, NULL);

   printf("# ec_get_edtime: %d\n", evc->ec_get_edtime(evc));
   printf("# ec_get_flush: %d\n", evc->ec_get_flush_delay(evc));
   evc->ec_set_edtime(evc, 15);
   printf("# ec_get_edtime: %d\n", evc->ec_get_edtime(evc));

   printf("# Subscribe event_type: %d\n", event_type);
   sge_mirror_subscribe(evc, SGE_TYPE_JOB, print_event, NULL, NULL, NULL, NULL);
   sge_mirror_subscribe(evc, SGE_TYPE_JATASK, print_event, NULL, NULL, NULL, NULL);

   printf("# Set flush...\n");
   evc->ec_set_flush(evc, sgeE_JOB_ADD, true, 0);
   evc->ec_set_flush(evc, sgeE_JOB_MOD, true, 0);
   evc->ec_set_flush(evc, sgeE_JOB_DEL, true, 0);
   evc->ec_set_flush(evc, sgeE_JATASK_ADD, true, 0);
   evc->ec_set_flush(evc, sgeE_JATASK_MOD, true, 0);
   evc->ec_set_flush(evc, sgeE_JATASK_DEL, true, 0);

   while(!shut_me_down) {
      printf("# Processing events...\n");
      sge_mirror_error error = sge_mirror_process_events(evc);
      print_json("{s:s}", "event", "END_BATCH");
      if (error == SGE_EM_TIMEOUT && !shut_me_down) {
         printf("# error was SGE_EM_TIMEOUT\n");
         sleep(10);
         continue;
      }
   }

   sge_mirror_shutdown(evc);
}

