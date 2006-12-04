/*
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Red Hat, Inc., nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Worst. Preauthentication. Scheme. Ever. */

#ident "$Id$"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <arpa/inet.h>
#include <stdio.h>

#include <krb5/krb5.h>
#include <krb5/preauth_plugin.h>

/* This is not a standardized value.  It's defined here only to make it easier
 * to change in this module. */
#define KRB5_PADATA_WPSE_REQ 131

static int
client_get_flags(krb5_context kcontext, krb5_preauthtype pa_type)
{
    return PA_REAL;
}

static krb5_error_code
client_init(krb5_context kcontext, void **ctx)
{
    int *pctx;

    pctx = malloc(sizeof(int));
    if (pctx == NULL)
	return ENOMEM;
    *pctx = 0;
    *ctx = pctx;
    return 0;
}

static void
client_fini(krb5_context kcontext, void *ctx)
{
    int *pctx;

    pctx = ctx;
    if (pctx) {
#ifdef DEBUG
        fprintf(stderr, "wpse module called total of %d times\n", *pctx);
#endif
        free(pctx);
    }
}

static krb5_error_code
client_process(krb5_context kcontext,
	       void *plugin_context,
	       void *request_context,
	       krb5_get_init_creds_opt *opt,
	       preauth_get_client_data_proc client_get_data_proc,
	       struct _krb5_preauth_client_rock *rock,
	       krb5_kdc_req *request,
	       krb5_data *encoded_request_body,
	       krb5_data *encoded_previous_request,
	       krb5_pa_data *pa_data,
	       krb5_prompter_fct prompter,
	       void *prompter_data,
	       preauth_get_as_key_proc gak_fct,
	       void *gak_data,
	       krb5_data *salt, krb5_data *s2kparams,
	       krb5_keyblock *as_key,
	       krb5_pa_data **out_pa_data)
{
    krb5_pa_data *send_pa;
    krb5_int32 nnonce, enctype;
    krb5_keyblock *kb;
    krb5_error_code status;
    int *pctx;

#ifdef DEBUG
    fprintf(stderr, "%d bytes of preauthentication data (type %d)\n",
	    pa_data->length, pa_data->pa_type);
#endif

    pctx = plugin_context;
    if (pctx) {
	(*pctx)++;
    }

    if (pa_data->length == 0) {
	/* Create preauth data. */
	send_pa = malloc(sizeof(krb5_pa_data));
	if (send_pa == NULL)
	    return ENOMEM;
	send_pa->pa_type = KRB5_PADATA_WPSE_REQ;
	send_pa->length = 4;
	send_pa->contents = malloc(4);
	if (send_pa->contents == NULL) {
	    free(send_pa);
	    return ENOMEM;
	}
	/* Store the preauth data. */
	nnonce = htonl(request->nonce);
	memcpy(send_pa->contents, &nnonce, 4);
	*out_pa_data = send_pa;
    } else {
	/* A reply from the KDC.  Conventionally this would be
	 * indicated by a different preauthentication type, but this
	 * mechanism/implementation doesn't do that. */
	if (pa_data->length > 4) {
	    memcpy(&enctype, pa_data->contents, 4);
	    kb = NULL;
	    status = krb5_init_keyblock(kcontext, ntohl(enctype),
					pa_data->length - 4, &kb);
	    if (status != 0)
		return status;
	    memcpy(kb->contents, pa_data->contents + 4, pa_data->length - 4);
#ifdef DEBUG
	    fprintf(stderr, "Recovered key type=%d, length=%d.\n",
		    kb->enctype, kb->length);
#endif
	    status = krb5_copy_keyblock_contents(kcontext, kb, as_key);
	    krb5_free_keyblock(kcontext, kb);
	    return status;
	}
	return KRB5KRB_ERR_GENERIC;
    }
    return 0;
}

#define WPSE_MAGIC 0x77707365
typedef struct _wpse_req_ctx
{
    int magic;
    int value;
} wpse_req_ctx;

static void
client_req_init(krb5_context kcontext, void *plugin_context, void **req_context_p)
{
    wpse_req_ctx *ctx;

    *req_context_p = NULL;

    /* Allocate a request context. Useful for verifying that we do in fact
     * do per-request cleanup. */
    ctx = (wpse_req_ctx *) malloc(sizeof(*ctx));
    if (ctx == NULL)
	return;
    ctx->magic = WPSE_MAGIC;
    ctx->value = 0xc0dec0de;

    *req_context_p = ctx;
}

static void
client_req_cleanup(krb5_context kcontext, void *plugin_context, void *req_context)
{
    wpse_req_ctx *ctx = (wpse_req_ctx *)req_context;

    if (ctx) {
#ifdef DEBUG
	fprintf(stderr, "client_req_cleanup: req_ctx at %p has magic %x and value %x\n",
		ctx, ctx->magic, ctx->value);
#endif
	if (ctx->magic != WPSE_MAGIC) {
#ifdef DEBUG
	    fprintf(stderr, "client_req_cleanup: req_context at %p has bad magic value %x\n",
		    ctx, ctx->magic);
#endif
	    return;
	}
	free(ctx);
    }
    return;
}

static krb5_error_code
client_gic_opt(krb5_context kcontext,
	       void *plugin_context,
	       krb5_get_init_creds_opt *opt,
	       krb5_principal principal,
	       const char *user_id,
	       const char *password,
	       krb5_prompter_fct prompter,
	       void *prompter_data,
	       int num_preauth_data,
	       krb5_gic_opt_pa_data *preauth_data)
{
    int i;
#ifdef DEBUG
    fprintf(stderr, "(wpse) client_gic_opt: received %d preauth_data items\n",
	    num_preauth_data);
    for (i = 0; i < num_preauth_data; i++) {
	fprintf(stderr, "  %3d: attribute '%s', value '%s'\n",
		i, preauth_data[i].attr, preauth_data[i].value);
    }
#endif
    return 0;
}


/* Free state. */
static krb5_error_code
server_free_pa_request_context(krb5_context kcontext, void *plugin_context,
			       void **request_context)
{
    if (*request_context != NULL) {
	free(*request_context);
	*request_context = NULL;
    }
    return 0;
}

/* Obtain and return any preauthentication data (which is destined for the
 * client) which matches type data->pa_type. */
static krb5_error_code
server_get_edata(krb5_context kcontext,
		 krb5_kdc_req *request,
		 struct _krb5_db_entry_new *client,
		 struct _krb5_db_entry_new *server,
		 preauth_get_entry_data_proc server_get_entry_data,
		 void *pa_module_context,
		 krb5_pa_data *data)
{
    /* Return zero bytes of data. */
    data->length = 0;
    data->contents = NULL;
    return 0;
}

/* Verify a request from a client. */
static krb5_error_code
server_verify(krb5_context kcontext,
	      struct _krb5_db_entry_new *client,
	      krb5_data *req_pkt,
	      krb5_kdc_req *request,
	      krb5_enc_tkt_part *enc_tkt_reply,
	      krb5_pa_data *data,
	      preauth_get_entry_data_proc server_get_entry_data,
	      void *pa_module_context,
	      void **pa_request_context,
	      krb5_data **e_data)
{
    krb5_int32 nnonce;
    krb5_data *test_edata;

    /* Verify the preauth data. */
    if (data->length != 4)
	return KRB5KDC_ERR_PREAUTH_FAILED;
    memcpy(&nnonce, data->contents, 4);
    nnonce = ntohl(nnonce);
    if (memcmp(&nnonce, &request->nonce, 4) != 0)
	return KRB5KDC_ERR_PREAUTH_FAILED;
    /* Note that preauthentication succeeded. */
    enc_tkt_reply->flags |= TKT_FLG_PRE_AUTH;
    enc_tkt_reply->flags |= TKT_FLG_HW_AUTH;
    /* Allocate a context. Useful for verifying that we do in fact do
     * per-request cleanup. */
    if (*pa_request_context == NULL)
	*pa_request_context = malloc(4);

    /* Return edata to exercise code that handles edata... */
    test_edata = malloc(sizeof(*test_edata));
    if (test_edata != NULL) {
	test_edata->data = malloc(20);
	if (test_edata->data == NULL) {
	    free(test_edata);
	} else {
	    test_edata->length = 20;
	    memset(test_edata->data, '#', 20); /* fill it with junk */
	    *e_data = test_edata;
	}
    }
    return 0;
}

/* Create the response for a client. */
static krb5_error_code
server_return(krb5_context kcontext,
	      krb5_pa_data *padata,
	      struct _krb5_db_entry_new *client,
	      krb5_data *req_pkt,
	      krb5_kdc_req *request,
	      krb5_kdc_rep *reply,
	      struct _krb5_key_data *client_key,
	      krb5_keyblock *encrypting_key,
	      krb5_pa_data **send_pa,
	      preauth_get_entry_data_proc server_get_entry_data,
	      void *pa_module_context,
	      void **pa_request_context)
{
    /* This module does a couple of dumb things.  It tags its reply with
     * the same type as the initial challenge (expecting the client to sort
     * out whether there's anything useful in there).  Oh, and it replaces
     * the AS reply key with one which is sent in the clear. */
    krb5_keyblock *kb;
    krb5_int32 enctype;
    int i;

    *send_pa = NULL;

    /* We'll want a key with the first supported enctype. */
    for (i = 0; i < request->nktypes; i++) {
	kb = NULL;
	if (krb5_init_keyblock(kcontext, request->ktype[i], 0, &kb) == 0) {
	    break;
	}
    }
    if (i >= request->nktypes) {
	/* No matching cipher type found. */
	return 0;
    }

    /* Randomize a key and save it for the client. */
    if (krb5_c_make_random_key(kcontext, request->ktype[i], kb) != 0) {
	krb5_free_keyblock(kcontext, kb);
	return 0;
    }
#ifdef DEBUG
    fprintf(stderr, "Generated random key, type=%d, length=%d.\n",
	    kb->enctype, kb->length);
#endif

    *send_pa = malloc(sizeof(krb5_pa_data));
    if (*send_pa == NULL) {
	krb5_free_keyblock(kcontext, kb);
	return ENOMEM;
    }
    (*send_pa)->pa_type = KRB5_PADATA_WPSE_REQ;
    (*send_pa)->length = 4 + kb->length;
    (*send_pa)->contents = malloc(4 + kb->length);
    if ((*send_pa)->contents == NULL) {
	free(*send_pa);
	*send_pa = NULL;
	krb5_free_keyblock(kcontext, kb);
	return ENOMEM;
    }

    /* Store the preauth data. */
    enctype = htonl(kb->enctype);
    memcpy((*send_pa)->contents, &enctype, 4);
    memcpy((*send_pa)->contents + 4, kb->contents, kb->length);
    krb5_free_keyblock_contents(kcontext, encrypting_key);
    krb5_copy_keyblock_contents(kcontext, kb, encrypting_key);

    /* Clean up. */
    krb5_free_keyblock(kcontext, kb);

    return 0;
}

static int
server_get_flags(krb5_context kcontext, krb5_preauthtype pa_type)
{
    return PA_HARDWARE | PA_REPLACES_KEY;
}

static krb5_preauthtype supported_client_pa_types[] = {KRB5_PADATA_WPSE_REQ, 0};
static krb5_preauthtype supported_server_pa_types[] = {KRB5_PADATA_WPSE_REQ, 0};

struct krb5plugin_preauth_client_ftable_v0 preauthentication_client_0 = {
    "wpse",				    /* name */
    &supported_client_pa_types[0],	    /* pa_type_list */
    NULL,				    /* enctype_list */
    client_init,			    /* plugin init function */
    client_fini,			    /* plugin fini function */
    client_get_flags,			    /* get flags function */
    client_req_init,			    /* request init function */
    client_req_cleanup,			    /* request fini function */
    client_process,			    /* process function */
    NULL,				    /* try_again function */
    client_gic_opt			    /* get init creds opts function */
};

struct krb5plugin_preauth_server_ftable_v0 preauthentication_server_0 = {
    "wpse",
    &supported_server_pa_types[0],
    NULL,
    NULL,
    server_get_flags,
    server_get_edata,
    server_verify,
    server_return,
    server_free_pa_request_context,
};
