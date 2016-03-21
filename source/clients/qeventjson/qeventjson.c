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
#include "sched/sort_hosts.h"
#include "sched/sge_complex_schedd.h"

#include "msg_clients_common.h"
#include "msg_common.h"

#include "sge_answer.h"
#include "sge_mirror.h"
#include "sge_event.h"
#include "sge_time.h"
#include "sge_feature.h"
#include "sge_spool.h"
#include "qeventjson.h"
#include "sge_qquota.h"
#include "sge_qstat.h"
#include "sge_profiling.h"
#include "sge_mt_init.h"
#include "sgeobj/sge_job.h"
#include "sgeobj/sge_range.h"
#include "sgeobj/sge_host.h"
#include "sgeobj/sge_centry.h"
#include "sgeobj/sge_resource_quota.h"
#include "sgeobj/sge_ulong.h"

#include "gdi/sge_gdi_ctx.h"

#include "frozen.h"

/****** qquota_output/qquota_get_next_filter() *********************************
*  NAME
*     qquota_get_next_filter() -- tokenize rue_name of usage
*
*  SYNOPSIS
*     static char* qquota_get_next_filter(char *filter, const char *cp)
*
*  FUNCTION
*     The rue_name has the type /user_name/project_name/pe_name/queue_name/host_name.
*     This function tokenizes the rue_name and gives always one element back
*
*  INPUTS
*     char *filter   - store for the token
*     const char *cp - pointer to rue_name
*
*  RESULT
*     static char* - pointer for the next token
*
*  NOTES
*     MT-NOTE: qquota_get_next_filter() is not MT safe
*
*******************************************************************************/
static char *qquota_get_next_filter(stringT filter, const char *cp)
{
   char *ret = NULL;

   ret = strchr(cp, '/')+1;
   if (ret - cp < MAX_STRING_SIZE && ret - cp > 1) {
      snprintf(filter, ret - cp, "%s", cp);
   } else {
      sprintf(filter, "-");
   }

   return ret;
}

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
list_to_json_dstring(dstring *buffer, lList *list, int field) {
  const char *str;
  char json_tmp[250];
  bool first = true;
  lListElem *el;

  sge_dstring_append(buffer, "[");
  for_each(el, list) {
    if (!first) {
      sge_dstring_append(buffer, ",");
    }
    str = lGetString(el, field);
    json_emit_quoted_str(json_tmp, 50, str, strlen(str));
    sge_dstring_append(buffer, json_tmp);
    first = false;
  }
  sge_dstring_append(buffer, "]");
}

static void
format_json_as_dstring(dstring *buffer, const char *format, ...) {
   // print json
   char json_buffer[1024*12];
   va_list argptr;
   va_start(argptr, format);
   int jsonlen = json_emit_va(json_buffer, sizeof(json_buffer), format, argptr);
   va_end(argptr);
   if (jsonlen < sizeof(json_buffer)) {
     sge_dstring_append(buffer, json_buffer);
   }
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


static bool
print_qquota_filter_as_json(lListElem *filter, const char *name, const char *value,
                            dstring *buffer, bool prependComma) {
  if (filter != NULL) {
    if (prependComma)
       sge_dstring_append_char(buffer, ',');
    if (!lGetBool(filter, RQRF_expand) || value == NULL) {
      format_json_as_dstring(buffer, "s:{s:", name, "include");
      list_to_json_dstring(buffer, lGetList(filter, RQRF_scope), ST_name);
      sge_dstring_append(buffer, ",\"exclude\":");
      list_to_json_dstring(buffer, lGetList(filter, RQRF_xscope), ST_name);
      sge_dstring_append(buffer, "}");
    } else {
      format_json_as_dstring(buffer, "s:s", name, value);
    }
    return true;
  } else {
    return false;
  }
}

static void
print_quota_rule_as_json(const char *event, u_long32 timestamp, const char *key, lList *centry_list, lList *exechost_list, lListElem *rqs) {
   dstring json_buffer = DSTRING_INIT;
   format_json_as_dstring(&json_buffer, "{s:s,s:u,s:s,s:S,s:[",
                                        "event", event,
                                        "timestamp", timestamp,
                                        "key", (key == NULL ? lGetString(rqs, RQS_name) : key),
                                        "enabled", (lGetBool(rqs, RQS_enabled) ? "true" : "false"),
                                        "rules");

   lListElem* global_host = NULL;
   lListElem* exec_host = NULL;

   lListElem *rule = NULL;
   dstring rule_name = DSTRING_INIT;
   int rule_count = 1;
   bool firstRule = true;
   bool firstLimit = true;

   for_each(rule, lGetList(rqs, RQS_rule)) {
     lListElem *limit = NULL;

     if (lGetString(rule, RQR_name)) {
       sge_dstring_sprintf(&rule_name, "%s/%s", lGetString(rqs, RQS_name), lGetString(rule, RQR_name));
     } else {
       sge_dstring_sprintf(&rule_name, "%s/%d", lGetString(rqs, RQS_name), rule_count);
     }

     if (!firstRule)
       sge_dstring_append_char(&json_buffer, ',');
     firstRule = false;
     format_json_as_dstring(&json_buffer, "{s:s,s:[",
                                          "rule_name", sge_dstring_get_string(&rule_name),
                                          "limits");

     firstLimit = true;
     for_each(limit, lGetList(rule, RQR_limit)) {
       const char *limit_name = lGetString(limit, RQRL_name);
       lList *rue_list = lGetList(limit, RQRL_usage);
       lListElem *raw_centry = centry_list_locate(centry_list, limit_name);
       lListElem *rue_elem = NULL;

       if (raw_centry == NULL) {
         /* undefined centries can be ignored */
         continue;
       }

       if (lGetUlong(raw_centry, CE_consumable)) {
         /* for consumables we need to walk through the utilization and search for matching values */
         for_each(rue_elem, rue_list) {
           u_long32 dominant = 0;
           const char *rue_name = lGetString(rue_elem, RUE_name);
           char *cp = NULL;
           stringT user = "", project = "", pe = "", queue = "", host = "";
           dstring limit_str = DSTRING_INIT;
           dstring value_str = DSTRING_INIT;

           /* check user name */
           cp = qquota_get_next_filter(user, rue_name);
           /* check project */
           cp = qquota_get_next_filter(project, cp);
           /* check parallel environment */
           cp = qquota_get_next_filter(pe, cp);
           /* check cluster queue */
           cp = qquota_get_next_filter(queue, cp);
           /* check host name */
           cp = qquota_get_next_filter(host, cp);
           if (lGetBool(limit, RQRL_dynamic)) {
             exec_host = host_list_locate(exechost_list, host);
             sge_dstring_sprintf(&limit_str, "%d", (int)scaled_mixed_load(lGetString(limit, RQRL_value),
                                                                          global_host, exec_host, centry_list));

           } else {
             lSetDouble(raw_centry, CE_pj_doubleval, lGetDouble(limit, RQRL_dvalue));
             sge_get_dominant_stringval(raw_centry, &dominant, &limit_str);
           }

           lSetDouble(raw_centry,CE_pj_doubleval, lGetDouble(rue_elem, RUE_utilized_now));
           sge_get_dominant_stringval(raw_centry, &dominant, &value_str);

           // convert filters to json
           dstring filter_str = DSTRING_INIT;
           sge_dstring_append_char(&filter_str, '{');
           bool notFirstFilter = false;
           notFirstFilter = print_qquota_filter_as_json(lGetObject(rule, RQR_filter_users), "users", user, &filter_str, notFirstFilter) || notFirstFilter;
           notFirstFilter = print_qquota_filter_as_json(lGetObject(rule, RQR_filter_projects), "projects", project, &filter_str, notFirstFilter) || notFirstFilter;
           notFirstFilter = print_qquota_filter_as_json(lGetObject(rule, RQR_filter_pes), "pes", pe, &filter_str, notFirstFilter) || notFirstFilter;
           notFirstFilter = print_qquota_filter_as_json(lGetObject(rule, RQR_filter_queues), "queues", queue, &filter_str, notFirstFilter) || notFirstFilter;
           notFirstFilter = print_qquota_filter_as_json(lGetObject(rule, RQR_filter_hosts), "hosts", host, &filter_str, notFirstFilter) || notFirstFilter;
           sge_dstring_append_char(&filter_str, '}');

           if (!firstLimit)
             sge_dstring_append_char(&json_buffer, ',');
           firstLimit = false;
           format_json_as_dstring(&json_buffer,
                      "{s:s,s:s,s:s,s:s,s:s,s:s,s:S,s:s,s:s}",
                      "limit_name", limit_name,
                      "user", user,
                      "project", project,
                      "pe", pe,
                      "queue", queue,
                      "host", host,
                      "filters", sge_dstring_get_string(&filter_str),
                      "usage_value", sge_dstring_get_string(&value_str),
                      "limit_value", sge_dstring_get_string(&limit_str));

           sge_dstring_free(&filter_str);
           sge_dstring_free(&limit_str);
           sge_dstring_free(&value_str);
         }
       } else {
         /* static values */
         if (!firstLimit)
           sge_dstring_append_char(&json_buffer, ',');
         firstLimit = false;
         format_json_as_dstring(&json_buffer,
                    "{s:s,s:s}",
                    "limit_name", limit_name,
                    "limit_value", lGetString(limit, RQRL_value));
       }
     }

     sge_dstring_append(&json_buffer, "]}");
   }
   rule_count++;

   sge_dstring_append(&json_buffer, "]}");
   printf("%s\n", sge_dstring_get_string(&json_buffer));

   sge_dstring_free(&rule_name);
   sge_dstring_free(&json_buffer);
}

static void
print_host_as_json(const char *event, u_long32 timestamp, lList *host_list, lListElem *host, lList *centry_list) {
   dstring json_buffer = DSTRING_INIT;
   format_json_as_dstring(&json_buffer, "{s:s,s:u,s:s,s:{",
                                        "event", event,
                                        "timestamp", timestamp,
                                        "name", lGetHost(host, EH_name),
                                        "consumables");

   lListElem *rep;
   int first = 1;

   const lList *ce_values = lGetList(host, EH_consumable_config_list);
   for_each(rep, ce_values) {
     if (first > 1)
       sge_dstring_append_char(&json_buffer, ',');
     format_json_as_dstring(&json_buffer, "s:s", lGetString(rep, CE_name), lGetString(rep, CE_stringval));
     first++;
   }

   format_json_as_dstring(&json_buffer, "},s:{",
                                        "values");

   lList *rlp = NULL;
   char dom[5];
   dstring resource_string = DSTRING_INIT;
   const char *s;
   u_long32 dominant;

   first = 1;
   host_complexes2scheduler(&rlp, host, host_list, centry_list);
   for_each(rep, rlp) {
      u_long32 type = lGetUlong(rep, CE_valtype);

      sge_dstring_clear(&resource_string);

      switch (type) {
         case TYPE_HOST:
         case TYPE_STR:
         case TYPE_CSTR:
         case TYPE_RESTR:
            if (!(lGetUlong(rep, CE_pj_dominant)&DOMINANT_TYPE_VALUE)) {
               dominant = lGetUlong(rep, CE_pj_dominant);
               s = lGetString(rep, CE_pj_stringval);
            } else {
               dominant = lGetUlong(rep, CE_dominant);
               s = lGetString(rep, CE_stringval);
            }
            break;
         case TYPE_TIM:
            if (!(lGetUlong(rep, CE_pj_dominant)&DOMINANT_TYPE_VALUE)) {
               double val = lGetDouble(rep, CE_pj_doubleval);

               dominant = lGetUlong(rep, CE_pj_dominant);
               double_print_time_to_dstring(val, &resource_string);
               s = sge_dstring_get_string(&resource_string);
            } else {
               double val = lGetDouble(rep, CE_doubleval);

               dominant = lGetUlong(rep, CE_dominant);
               double_print_time_to_dstring(val, &resource_string);
               s = sge_dstring_get_string(&resource_string);
            }
            break;
         case TYPE_MEM:
            if (!(lGetUlong(rep, CE_pj_dominant)&DOMINANT_TYPE_VALUE)) {
               double val = lGetDouble(rep, CE_pj_doubleval);

               dominant = lGetUlong(rep, CE_pj_dominant);
               double_print_memory_to_dstring(val, &resource_string);
               s = sge_dstring_get_string(&resource_string);
            } else {
               double val = lGetDouble(rep, CE_doubleval);

               dominant = lGetUlong(rep, CE_dominant);
               double_print_memory_to_dstring(val, &resource_string);
               s = sge_dstring_get_string(&resource_string);
            }
            break;
         default:
            if (!(lGetUlong(rep, CE_pj_dominant)&DOMINANT_TYPE_VALUE)) {
               double val = lGetDouble(rep, CE_pj_doubleval);

               dominant = lGetUlong(rep, CE_pj_dominant);
               double_print_to_dstring(val, &resource_string);
               s = sge_dstring_get_string(&resource_string);
            } else {
               double val = lGetDouble(rep, CE_doubleval);

               dominant = lGetUlong(rep, CE_dominant);
               double_print_to_dstring(val, &resource_string);
               s = sge_dstring_get_string(&resource_string);
            }
            break;
      }
      monitor_dominance(dom, dominant);
      switch(lGetUlong(rep, CE_valtype)) {
         case TYPE_INT:
         case TYPE_DOUBLE:
            if (first > 1)
              sge_dstring_append_char(&json_buffer, ',');
            format_json_as_dstring(&json_buffer, "s:S", lGetString(rep, CE_name), s);
            first++;
            break;
         case TYPE_TIM:
         case TYPE_MEM:
         case TYPE_BOO:
         default:
            if (first > 1)
              sge_dstring_append_char(&json_buffer, ',');
            format_json_as_dstring(&json_buffer, "s:s", lGetString(rep, CE_name), s);
            first++;
            break;
      }
   }
   lFreeList(&rlp);
   sge_dstring_free(&resource_string);

   sge_dstring_append(&json_buffer, "}}");
   printf("%s\n", sge_dstring_get_string(&json_buffer));
   sge_dstring_free(&json_buffer);
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
     print_json("{s:s}", "event", "JOB_LIST_START");

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
     u_long cpu = 0;
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
       } else if (strcmp("cpu", fieldName) == 0) {
         cpu = (u_long) lGetDouble(ua, UA_value);
       } else if (strcmp("end_time", fieldName) == 0) {
         end_time = (u_long) lGetDouble(ua, UA_value);
       } else if (strcmp("exit_status", fieldName) == 0) {
         exit_status = (u_long) lGetDouble(ua, UA_value);
       }
       ua = lNext(ua);
     }

     if (event_type == sgeE_JOB_USAGE) {
       print_json("{s:s,s:u,s:u,s:u,s:u,s:u,s:u}",
                  "event", "JOB_USAGE",
                  "timestamp", timestamp,
                  "job_id", job_id,
                  "task_id", task_id,
                  "vmem", vmem,
                  "maxvmem", maxvmem,
                  "cpu", cpu);
     } else {
       print_json("{s:s,s:u,s:u,s:u,s:u,s:u,s:u,s:u,s:u}",
                  "event", "JOB_FINAL_USAGE",
                  "timestamp", timestamp,
                  "job_id", job_id,
                  "task_id", task_id,
                  "vmem", vmem,
                  "maxvmem", maxvmem,
                  "cpu", cpu,
                  "end_time", end_time,
                  "exit_status", exit_status);
     }

   } else if (event_type == sgeE_RQS_LIST) {
     // initial resource quota list
     lList *centry_list = *sge_master_list(object_base, SGE_TYPE_CENTRY);
     lList *exechost_list = *sge_master_list(object_base, SGE_TYPE_EXECHOST);

     lList *rqs_list = lGetList(event,ET_new_version);
     lListElem *rqs = NULL;

     for_each(rqs, rqs_list) {
       print_quota_rule_as_json("RQS_LIST", timestamp, NULL, centry_list, exechost_list, rqs);
     }

   } else if (event_type == sgeE_RQS_ADD ||
              event_type == sgeE_RQS_MOD) {
     // event for resource quota rule
     lList *centry_list = *sge_master_list(object_base, SGE_TYPE_CENTRY);
     lList *exechost_list = *sge_master_list(object_base, SGE_TYPE_EXECHOST);

     const char *key = lGetString(event, ET_strkey);

     lList *rqs_list = lGetList(event,ET_new_version);
     lListElem *rqs = lFirst(rqs_list);

     print_quota_rule_as_json((event_type == sgeE_RQS_ADD ? "RQS_ADD" : "RQS_MOD"), timestamp, key, centry_list, exechost_list, rqs);

   } else if (event_type == sgeE_RQS_DEL) {
     // event for resource quota rule
     const char *key = lGetString(event, ET_strkey);
     print_json("{s:s,s:u,s:s}",
                "event", "RQS_DEL",
                "timestamp", timestamp,
                "key", key);

   } else if (event_type == sgeE_EXECHOST_LIST ||
              event_type == sgeE_EXECHOST_ADD ||
              event_type == sgeE_EXECHOST_MOD) {
     // initial exechost list
     lList *centry_list = *sge_master_list(object_base, SGE_TYPE_CENTRY);
     lList *host_list = lGetList(event,ET_new_version);
     lListElem *host = NULL;

     char *event_str = NULL;
     switch (event_type) {
       case sgeE_EXECHOST_LIST: event_str = "EXECHOST_LIST"; break;
       case sgeE_EXECHOST_ADD: event_str = "EXECHOST_ADD"; break;
       case sgeE_EXECHOST_MOD: event_str = "EXECHOST_MOD"; break;
     }

     for_each(host, host_list) {
       print_host_as_json(event_str, timestamp, host_list, host, centry_list);
     }

   } else if (event_type == sgeE_EXECHOST_DEL) {
     // event for exechost
     const char *key = lGetString(event, ET_strkey);
     print_json("{s:s,s:u,s:s}",
                "event", "EXECHOST_DEL",
                "timestamp", timestamp,
                "name", key);
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
   
   sge_mirror_initialize(evc, EV_ID_ANY, "qeventjson", true,
                         NULL, NULL, NULL, NULL, NULL);
   sge_mirror_subscribe(evc, SGE_TYPE_SHUTDOWN, print_event, NULL, NULL, NULL, NULL);
   sge_mirror_subscribe(evc, SGE_TYPE_ADMINHOST, print_event, NULL, NULL, NULL, NULL);

   printf("# ec_get_edtime: %d\n", evc->ec_get_edtime(evc));
   printf("# ec_get_flush: %d\n", evc->ec_get_flush_delay(evc));
   evc->ec_set_edtime(evc, 15);
   printf("# ec_get_edtime: %d\n", evc->ec_get_edtime(evc));

   printf("# Subscribe event_type: %d\n", event_type);
   // mirror job and job task events
   sge_mirror_subscribe(evc, SGE_TYPE_ALL, print_event, NULL, NULL, NULL, NULL);

   printf("# Set flush...\n");
   // set the flush time for these events to 0 (immediate)
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
         print_json("{s:s,s:s}", "event", "SGE_ERROR", "error", "SGE_EM_TIMEOUT");
         sleep(10);
         continue;
      }
   }

   sge_mirror_shutdown(evc);
}

