/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

/* FIXME: Explain the contents of this file */
/*
 * A first guess.  This file contains routines to manage memory-to-memory
 * copies of buffers described by MPI datatypes
 */

/* MPIDI_COPY_BUFFER_SZ is the size of the buffer that is allocated when 
   copying from one non-contiguous buffer to another.  In this case, an 
   intermediate contiguous buffer is used of this size */
#if !defined(MPIDI_COPY_BUFFER_SZ)
#define MPIDI_COPY_BUFFER_SZ 16384
#endif

/* FIXME: Explain this routine .
Used indirectly by mpid_irecv, mpid_recv (through MPIDI_CH3_RecvFromSelf) and 
 in mpidi_isend_self.c */

/* This routine appears to handle copying data from one buffer (described by
 the usual MPI triple (buf,count,datatype) to another, as is needed in send 
 and receive from self.  We may want to put all of the "from self" routines
 into a single file, and make MPIDI_CH3U_Buffer_copy static to this file. */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Buffer_copy
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
void MPIDI_CH3U_Buffer_copy(
    const void * const sbuf, MPI_Aint scount, MPI_Datatype sdt, int * smpi_errno,
    void * const rbuf, MPI_Aint rcount, MPI_Datatype rdt, MPIDI_msg_sz_t * rsz,
    int * rmpi_errno)
{
    int sdt_contig;
    int rdt_contig;
    MPI_Aint sdt_true_lb, rdt_true_lb;
    MPIDI_msg_sz_t sdata_sz;
    MPIDI_msg_sz_t rdata_sz;
    MPID_Datatype * sdt_ptr;
    MPID_Datatype * rdt_ptr;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_BUFFER_COPY);
    MPIDI_STATE_DECL(MPID_STATE_MEMCPY);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_BUFFER_COPY);
    *smpi_errno = MPI_SUCCESS;
    *rmpi_errno = MPI_SUCCESS;

    MPIDI_Datatype_get_info(scount, sdt, sdt_contig, sdata_sz, sdt_ptr, sdt_true_lb);
    MPIDI_Datatype_get_info(rcount, rdt, rdt_contig, rdata_sz, rdt_ptr, rdt_true_lb);

    /* --BEGIN ERROR HANDLING-- */
    if (sdata_sz > rdata_sz)
    {
	MPIU_DBG_MSG_FMT(CH3_OTHER,TYPICAL,(MPIU_DBG_FDEST,
	    "message truncated, sdata_sz=" MPIDI_MSG_SZ_FMT " rdata_sz=" MPIDI_MSG_SZ_FMT,
			  sdata_sz, rdata_sz));
	sdata_sz = rdata_sz;
	*rmpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_TRUNCATE, "**truncate", "**truncate %d %d", sdata_sz, rdata_sz );
    }
    /* --END ERROR HANDLING-- */
    
    if (sdata_sz == 0)
    {
	*rsz = 0;
	goto fn_exit;
    }
    
    if (sdt_contig && rdt_contig)
    {
	MPIDI_FUNC_ENTER(MPID_STATE_MEMCPY);
	MPIU_Memcpy((char *)rbuf + rdt_true_lb, (const char *)sbuf + sdt_true_lb, sdata_sz);
	MPIDI_FUNC_EXIT(MPID_STATE_MEMCPY);
	*rsz = sdata_sz;
    }
    else if (sdt_contig)
    {
	MPID_Segment seg;
	MPI_Aint last;

	MPID_Segment_init(rbuf, rcount, rdt, &seg, 0);
	last = sdata_sz;
	MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST, 
                          "pre-unpack last=" MPIDI_MSG_SZ_FMT, last ));
	MPID_Segment_unpack(&seg, 0, &last, (char*)sbuf + sdt_true_lb);
	MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
			 "pre-unpack last=" MPIDI_MSG_SZ_FMT, last ));
	/* --BEGIN ERROR HANDLING-- */
	if (last != sdata_sz)
	{
	    *rmpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_TYPE, "**dtypemismatch", 0);
	}
	/* --END ERROR HANDLING-- */

	*rsz = last;
    }
    else if (rdt_contig)
    {
	MPID_Segment seg;
	MPI_Aint last;

	MPID_Segment_init(sbuf, scount, sdt, &seg, 0);
	last = sdata_sz;
	MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
			       "pre-pack last=" MPIDI_MSG_SZ_FMT, last ));
	MPID_Segment_pack(&seg, 0, &last, (char*)rbuf + rdt_true_lb);
	MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
			    "post-pack last=" MPIDI_MSG_SZ_FMT, last ));
	/* --BEGIN ERROR HANDLING-- */
	if (last != sdata_sz)
	{
	    *rmpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_TYPE, "**dtypemismatch", 0);
	}
	/* --END ERROR HANDLING-- */

	*rsz = last;
    }
    else
    {
	char * buf;
	MPIDI_msg_sz_t buf_off;
	MPID_Segment sseg;
	MPIDI_msg_sz_t sfirst;
	MPID_Segment rseg;
	MPIDI_msg_sz_t rfirst;

	buf = MPIU_Malloc(MPIDI_COPY_BUFFER_SZ);
	/* --BEGIN ERROR HANDLING-- */
	if (buf == NULL)
	{
	    MPIU_DBG_MSG(CH3_OTHER,TYPICAL,"SRBuf allocation failure");
	    *smpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
	    *rmpi_errno = *smpi_errno;
	    *rsz = 0;
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */

	MPID_Segment_init(sbuf, scount, sdt, &sseg, 0);
	MPID_Segment_init(rbuf, rcount, rdt, &rseg, 0);

	sfirst = 0;
	rfirst = 0;
	buf_off = 0;
	
	for(;;)
	{
	    MPI_Aint last;
	    char * buf_end;

	    if (sdata_sz - sfirst > MPIDI_COPY_BUFFER_SZ - buf_off)
	    {
		last = sfirst + (MPIDI_COPY_BUFFER_SZ - buf_off);
	    }
	    else
	    {
		last = sdata_sz;
	    }
	    
	    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
               "pre-pack first=" MPIDI_MSG_SZ_FMT ", last=" MPIDI_MSG_SZ_FMT, 
						sfirst, last ));
	    MPID_Segment_pack(&sseg, sfirst, &last, buf + buf_off);
	    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
               "post-pack first=" MPIDI_MSG_SZ_FMT ", last=" MPIDI_MSG_SZ_FMT, 
               sfirst, last ));
	    /* --BEGIN ERROR HANDLING-- */
	    MPIU_Assert(last > sfirst);
	    /* --END ERROR HANDLING-- */
	    
	    buf_end = buf + buf_off + (last - sfirst);
	    sfirst = last;
	    
	    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
             "pre-unpack first=" MPIDI_MSG_SZ_FMT ", last=" MPIDI_MSG_SZ_FMT, 
						rfirst, last ));
	    MPID_Segment_unpack(&rseg, rfirst, &last, buf);
	    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
             "post-unpack first=" MPIDI_MSG_SZ_FMT ", last=" MPIDI_MSG_SZ_FMT, 
						rfirst, last ));
	    /* --BEGIN ERROR HANDLING-- */
	    MPIU_Assert(last > rfirst);
	    /* --END ERROR HANDLING-- */

	    rfirst = last;

	    if (rfirst == sdata_sz)
	    {
		/* successful completion */
		break;
	    }

	    /* --BEGIN ERROR HANDLING-- */
	    if (sfirst == sdata_sz)
	    {
		/* datatype mismatch -- remaining bytes could not be unpacked */
		*rmpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_TYPE, "**dtypemismatch", 0);
		break;
	    }
	    /* --END ERROR HANDLING-- */

	    buf_off = sfirst - rfirst;
	    if (buf_off > 0)
	    {
		MPIU_DBG_MSG_FMT(CH3_OTHER, VERBOSE, (MPIU_DBG_FDEST,
                  "moved " MPIDI_MSG_SZ_FMT " bytes to the beginning of the tmp buffer", buf_off));
		memmove(buf, buf_end - buf_off, buf_off);
	    }
	}

	*rsz = rfirst;
	MPIU_Free(buf);
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_BUFFER_COPY);
}


/*
 * This routine is called by mpid_recv and mpid_irecv when a request
 * matches a send-to-self message 
 */
#if defined(FINEGRAIN_MPI)
int MPIDI_CH3_RecvFromSelf( MPID_Request *rreq, void ** buf_handle, MPI_Aint count,
			    MPI_Datatype datatype )
#else
int MPIDI_CH3_RecvFromSelf( MPID_Request *rreq, void *buf, MPI_Aint count,
			    MPI_Datatype datatype )
#endif
{
    MPID_Request * const sreq = rreq->partner_request;
    int mpi_errno = MPI_SUCCESS;

    if (sreq != NULL)
    {
	MPIDI_msg_sz_t data_sz;

#if defined(FINEGRAIN_MPI)
        /* FG: Zerocopy */
        void * buf = (void *) (*buf_handle);
        if ( MPIDI_Request_get_self_zerocopy_flag(sreq) && MPIDI_Request_get_self_zerocopy_flag(rreq) )
        {
            int rdt_contig;
            MPI_Aint rdt_true_lb;
            MPID_Datatype * rdt_ptr;

            /* Unexpected Send-Collocated MPIX_Zsend/Izsend - MPIX_Zrecv/Izrecv pairing */
            MPIU_Assert(NULL == rreq->dev.user_buf);
            *(rreq->dev.user_buf_handle) = (void*) (*(sreq->dev.user_buf_handle));

            MPIDI_Datatype_get_info(count, datatype, rdt_contig, data_sz, rdt_ptr, rdt_true_lb);

            /* MPIX_Zsend buf_handle can't be set to NULL as we don't have
               a ptr to void **.  */
        }
        else if( MPIDI_Request_get_self_zerocopy_flag(sreq) && !MPIDI_Request_get_self_zerocopy_flag(rreq) ){
            /* Unexpected Send-Collocated MPIX_Zsend/Izsend<=>MPI_Recv/Irecv pairing. Freeing sender buffer */
            MPIDI_CH3U_Buffer_copy(*(sreq->dev.user_buf_handle), sreq->dev.user_count,
                                   sreq->dev.datatype, &sreq->status.MPI_ERROR,
                                   buf, count, datatype, &data_sz,
                                   &rreq->status.MPI_ERROR);

            /* Free the sender's buffer */
            MPIU_Free(*(sreq->dev.user_buf_handle));
        }
        else if( !MPIDI_Request_get_self_zerocopy_flag(sreq) && MPIDI_Request_get_self_zerocopy_flag(rreq) ){
            /* Unexpected Send-Collocated MPI_Send/Isend - MPIX_Zrecv/Izrecv pairing. Allocating receiver's buffer. */
            MPIU_Assert(NULL == rreq->dev.user_buf);
            /* Added checks for buffer count size as is done in MPIDI_CH3U_Buffer_copy() */
            MPIDI_CH3U_Buffer_allocate(sreq->dev.user_buf, sreq->dev.user_count,
                                       sreq->dev.datatype, &sreq->status.MPI_ERROR,
                                       rreq->dev.user_buf_handle, rreq->dev.user_count,
                                       rreq->dev.datatype, &data_sz, &rreq->status.MPI_ERROR);
            MPIDI_CH3U_Buffer_copy(sreq->dev.user_buf, sreq->dev.user_count,
                                   sreq->dev.datatype, &sreq->status.MPI_ERROR,
                                   *(rreq->dev.user_buf_handle), rreq->dev.user_count,
                                   rreq->dev.datatype, &data_sz, &rreq->status.MPI_ERROR);
        } else {
            /* Unexpected Send-Collocated MPI_Send/Isend - MPI_Recv/Irecv pairing */
#endif /* matches #if defined(FINEGRAIN_MPI) */

	MPIDI_CH3U_Buffer_copy(sreq->dev.user_buf, sreq->dev.user_count,
			       sreq->dev.datatype, &sreq->status.MPI_ERROR,
			       buf, count, datatype, &data_sz, 
			       &rreq->status.MPI_ERROR);
#if defined(FINEGRAIN_MPI)
        }
#endif

	MPIR_STATUS_SET_COUNT(rreq->status, data_sz);
	mpi_errno = MPID_Request_complete(sreq);
        if (mpi_errno != MPI_SUCCESS) {
            MPIR_ERR_POP(mpi_errno);
        }
    }
    else
    {
	/* The sreq is missing which means an error occurred.  
	   rreq->status.MPI_ERROR should have been set when the
	   error was detected. */
    }
    
    /* no other thread can possibly be waiting on rreq, so it is safe to 
       reset ref_count and cc */
    mpi_errno = MPID_Request_complete(rreq);
    if (mpi_errno != MPI_SUCCESS) {
        MPIR_ERR_POP(mpi_errno);
    }

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#if defined(FINEGRAIN_MPI)
/* FG: Zerocopy */
/* This routine handles allocation of the receive buffer for the
   matching pairing of MPI_Send/Isend with MPIX_Zrecv/Izrecv */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Buffer_allocate
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
void MPIDI_CH3U_Buffer_allocate(
    const void * const sbuf, MPI_Aint scount, MPI_Datatype sdt, int * smpi_errno,
    void ** rbuf_handle, MPI_Aint rcount, MPI_Datatype rdt, MPIDI_msg_sz_t * rsz,
    int * rmpi_errno)
{
    int sdt_contig;
    int rdt_contig;
    MPI_Aint sdt_true_lb, rdt_true_lb;
    MPIDI_msg_sz_t sdata_sz;
    MPIDI_msg_sz_t rdata_sz;
    MPID_Datatype * sdt_ptr;
    MPID_Datatype * rdt_ptr;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_BUFFER_ALLOCATE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_BUFFER_ALLOCATE);
    *smpi_errno = MPI_SUCCESS;
    *rmpi_errno = MPI_SUCCESS;

    MPIDI_Datatype_get_info(scount, sdt, sdt_contig, sdata_sz, sdt_ptr, sdt_true_lb);
    MPIDI_Datatype_get_info(rcount, rdt, rdt_contig, rdata_sz, rdt_ptr, rdt_true_lb);

    /* --BEGIN ERROR HANDLING-- */
    if (sdata_sz > rdata_sz)
    {
	MPIU_DBG_MSG_FMT(CH3_OTHER,TYPICAL,(MPIU_DBG_FDEST,
	    "message truncated, sdata_sz=" MPIDI_MSG_SZ_FMT " rdata_sz=" MPIDI_MSG_SZ_FMT,
			  sdata_sz, rdata_sz));
	sdata_sz = rdata_sz;
	*rmpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_TRUNCATE, "**truncate", "**truncate %d %d", sdata_sz, rdata_sz );
    }
    /* --END ERROR HANDLING-- */

    if (sdata_sz == 0)
    {
	*rsz = 0;
	goto fn_exit;
    }

    if (sdt_contig && rdt_contig)
    {
	*rbuf_handle = (void *)MPIU_Malloc(sdata_sz);
        MPIU_Assert(*rbuf_handle);
	*rsz = sdata_sz;
    }
    else
    {
	/* --BEGIN ERROR HANDLING-- */

        MPIU_DBG_MSG(CH3_OTHER,TYPICAL,"Sender and receiver datatypes are not contiguous");
        *smpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**zcopybufalloc", "**zcopybufalloc %d %d", scount, rcount);
        *rmpi_errno = *smpi_errno;
        *rsz = 0;
        goto fn_exit;

	/* --END ERROR HANDLING-- */
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_BUFFER_ALLOCATE);
}


/* This routine handles freeing of the sender buffer for the
   matching pairing of MPIX_Izend (coupled with MPI_Wait or MPI_Test
   variants) with Non-Collocated receiver. */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Buffer_free
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
void MPIDI_CH3U_Buffer_free( MPID_Request * request_ptr )
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_BUFFER_FREE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_BUFFER_FREE);

    if ( ( MPIDI_REQUEST_TYPE_SEND == MPIDI_Request_get_type(request_ptr) ) &&
         ( MPIDI_Request_get_self_zerocopy_flag(request_ptr) ) &&
         ( !(FG_is_within_same_HWP(request_ptr->dev.match.parts.dest_rank, request_ptr->comm, NULL)) ) )  {

        /* MPI_Izsend<=>MPI_Wait/Test (and variants) pairing: Freeing buf_handle */

        MPIU_Assert( (request_ptr->dev.user_buf_handle) && *(request_ptr->dev.user_buf_handle) );
        MPIU_Free(*(request_ptr->dev.user_buf_handle));
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_BUFFER_FREE);
}

#endif /* Matches #if defined(FINEGRAIN_MPI) */

