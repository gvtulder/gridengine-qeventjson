#ifndef __SGE_KRB_KTGT_L_H
#define __SGE_KRB_KTGT_L_H
/*___INFO__MARK_BEGIN__*/
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

#include "sge_boundaries.h"
#include "cull.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* *INDENT-OFF* */

/*
 * This is the list type we use to hold the client
 * TGT list for kerberos TGT forwarding
 */

enum {
    KTGT_id=KTGT_LOWERBOUND,
    KTGT_tgt
};

LISTDEF( KTGT_Type )
   SGE_ULONG(KTGT_id, CULL_DEFAULT)
   SGE_STRING(KTGT_tgt, CULL_DEFAULT)
LISTEND

NAMEDEF( KTGTN )
   NAME( "KTGT_id" )
   NAME( "KTGT_tgt" )
NAMEEND

/* *INDENT-ON* */

#define KTGTS sizeof(KTGTN)/sizeof(char*)

#ifdef  __cplusplus
}
#endif

#endif /* __SGE_KRBL_H*/

