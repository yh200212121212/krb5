/* -*- mode: c; indent-tabs-mode: nil -*- */
/*
 * lib/gssapi/krb5/prf.c
 *
 * Copyright 2009 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 */

#include <assert.h>
#include "k5-int.h"          /* for zap() */
#include "gssapiP_krb5.h"
#include <stdarg.h>

OM_uint32
krb5_gss_pseudo_random(OM_uint32 *minor_status,
                       gss_ctx_id_t context,
                       int prf_key,
                       const gss_buffer_t prf_in,
                       ssize_t desired_output_len,
                       gss_buffer_t prf_out)
{
    krb5_error_code code;
    krb5_key key = NULL;
    krb5_gss_ctx_id_t ctx;
    int i;
    krb5_keyblock *keyblock = NULL;
    OM_uint32 minor;
    size_t prflen;
    krb5_data t, ns;
    unsigned char *p;

    t.length = 0;
    t.data = NULL;
    ns.length = 0;
    ns.data = NULL;

    prf_out->length = 0;
    prf_out->value = NULL;

    if (!kg_validate_ctx_id(context)) {
        *minor_status = G_VALIDATE_FAILED;
        return GSS_S_NO_CONTEXT;
    }

    ctx = (krb5_gss_ctx_id_t)context;

    switch (prf_key) {
    case GSS_C_PRF_KEY_FULL:
        if (ctx->have_acceptor_subkey) {
            key = ctx->acceptor_subkey;
            break;
        }
        /* fallthrough */
    case GSS_C_PRF_KEY_PARTIAL:
        key = ctx->subkey;
        break;
    default:
        code = EINVAL;
        goto cleanup;
    }

    if (key == NULL) {
        code = EINVAL;
        goto cleanup;
    }

    prf_out->value = k5alloc(desired_output_len, &code);
    if (prf_out->value == NULL) {
        code = KG_INPUT_TOO_LONG;
        goto cleanup;
    }

    code = krb5_c_prf_length(ctx->k5_context,
                             krb5_k_key_enctype(ctx->k5_context, key),
                             &prflen);
    if (code != 0)
        goto cleanup;

    code = krb5_k_key_keyblock(ctx->k5_context, key, &keyblock);
    if (code != 0)
        goto cleanup;

    ns.length = 4 + prf_in->length;
    ns.data = k5alloc(ns.length, &code);
    if (ns.data == NULL) {
        code = KG_INPUT_TOO_LONG;
        goto cleanup;
    }

    memcpy(ns.data + 4, prf_in->value, prf_in->length);
    i = 0;
    p = (unsigned char *)prf_out->value;
    while (desired_output_len > 0) {
        store_32_be(i, ns.data);

        code = krb5_c_prf(ctx->k5_context, keyblock, &ns, &t);
        if (code != 0)
            goto cleanup;

        memcpy(p, t.data, MIN(t.length, desired_output_len));

        p += t.length;
        desired_output_len -= t.length;
        i++;

        krb5_free_data_contents(ctx->k5_context, &t);
    }

cleanup:
    if (code != 0)
        gss_release_buffer(&minor, prf_out);
    krb5_free_data_contents(ctx->k5_context, &ns);
    krb5_free_data_contents(ctx->k5_context, &t);
    krb5_free_keyblock(ctx->k5_context, keyblock);

    *minor_status = (OM_uint32)code;
    return (code == 0) ? GSS_S_COMPLETE : GSS_S_FAILURE;
}

