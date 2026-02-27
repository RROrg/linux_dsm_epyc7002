/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (c) 2021-2023 Synology inc.
*/

#include <linux/pagemap.h>
#include <linux/vfs.h>
#include <linux/falloc.h>
#include "cifsglob.h"
#include "smb2pdu.h"
#include "smb2proto.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_unicode.h"
#include "smb2status.h"
#include "smb2glob.h"

static struct {
	char *name;
} smb1_dialects_array[] = {
	{"\2NT LM 0.12"},
	{"\2Synology"},
	{"\2SMB 2.002"},
	{"\2SMB 2.???"},
	{NULL}
};
static __u16 smb2_dialects_array[] = {
	SMB20_PROT_ID,
	SMB21_PROT_ID,
	/**
	 *  SMB3 or above need cmac.ko to do smb signing
	 *  If CMAC not enable, the SMB3 will fail when packets need signing.
	 *  So we need to disable it
	 */ 
	SMB30_PROT_ID,
	SMB302_PROT_ID,
	SMB311_PROT_ID,
	BAD_PROT_ID
};
static int syno_next_header(char *buf);

void set_operation(struct smb_version_operations *dst, struct smb_version_operations *src)
{
	// SYNO must modify function:
	//    negotiate,

	dst->check_message = src->check_message;
	dst->next_header = src->next_header;
	dst->get_next_mid = src->get_next_mid;
	dst->find_mid = src->find_mid;

	// use origin function
	dst->get_dfs_refer = src->get_dfs_refer;
	dst->is_path_accessible = src->is_path_accessible;
	dst->query_path_info = src->query_path_info;
	dst->query_reparse_tag = src->query_reparse_tag;
	dst->get_srv_inum = src->get_srv_inum;
	dst->query_file_info = src->query_file_info;
	dst->set_path_size = src->set_path_size;
	dst->set_file_info = src->set_file_info;
	dst->posix_mkdir = src->posix_mkdir;
	dst->mkdir = src->mkdir;
	dst->rmdir = src->rmdir;
	dst->unlink = src->unlink;
	dst->rename_pending_delete = src->rename_pending_delete;
	dst->rename = src->rename;
	dst->create_hardlink = src->create_hardlink;
	dst->open = src->open;
	dst->queryfs = src->queryfs;

	dst->send_cancel = src->send_cancel;
	dst->compare_fids = src->compare_fids;
	dst->setup_request = src->setup_request;
	dst->setup_async_request = src->setup_async_request;
	dst->check_receive = src->check_receive;
	dst->add_credits = src->add_credits;
	dst->set_credits = src->set_credits;
	dst->get_credits_field = src->get_credits_field;
	dst->get_credits = src->get_credits;
	dst->wait_mtu_credits = src->wait_mtu_credits;
	dst->adjust_credits = src->adjust_credits;
	dst->revert_current_mid = src->revert_current_mid;
	dst->read_data_offset = src->read_data_offset;
	dst->read_data_length = src->read_data_length;
	dst->map_error = src->map_error;
	dst->dump_detail = src->dump_detail;
	dst->clear_stats = src->clear_stats;
	dst->print_stats = src->print_stats;
	dst->dump_share_caps = src->dump_share_caps;
	dst->is_oplock_break = src->is_oplock_break;
	dst->handle_cancelled_mid = src->handle_cancelled_mid;
	dst->downgrade_oplock = src->downgrade_oplock;
	dst->check_trans2 = src->check_trans2;
	dst->need_neg = src->need_neg;
	dst->negotiate_wsize = src->negotiate_wsize;
	dst->negotiate_rsize = src->negotiate_rsize;
	dst->sess_setup = src->sess_setup;
	dst->logoff = src->logoff;
	dst->tree_connect = src->tree_connect;
	dst->tree_disconnect = src->tree_disconnect;
	dst->qfs_tcon = src->qfs_tcon;
	dst->can_echo = src->can_echo;
	dst->echo = src->echo;
	dst->set_file_size = src->set_file_size;
	dst->set_compression = src->set_compression;
	dst->mkdir_setinfo = src->mkdir_setinfo;
	dst->query_symlink = src->query_symlink;
	dst->enum_snapshots = src->enum_snapshots;
	dst->notify = src->notify;
	dst->query_mf_symlink = src->query_mf_symlink;
	dst->create_mf_symlink = src->create_mf_symlink;
	dst->set_fid = src->set_fid;
	dst->close_getattr = src->close_getattr;
	dst->close = src->close;
	dst->flush = src->flush;
	dst->async_readv = src->async_readv;
	dst->async_writev = src->async_writev;
	dst->sync_read = src->sync_read;
	dst->sync_write = src->sync_write;
	dst->query_dir_first = src->query_dir_first;
	dst->query_dir_next = src->query_dir_next;
	dst->close_dir = src->close_dir;
	dst->calc_smb_size = src->calc_smb_size;
	dst->is_status_pending = src->is_status_pending;
	dst->is_session_expired = src->is_session_expired;
	dst->oplock_response = src->oplock_response;
	dst->mand_lock = src->mand_lock;
	dst->mand_unlock_range = src->mand_unlock_range;
	dst->push_mand_locks = src->push_mand_locks;
	dst->get_lease_key = src->get_lease_key;
	dst->set_lease_key = src->set_lease_key;
	dst->new_lease_key = src->new_lease_key;
	dst->generate_signingkey = src->generate_signingkey;
	dst->calc_signature = src->calc_signature;
	dst->set_integrity  = src->set_integrity;
	dst->is_read_op = src->is_read_op;
	dst->set_oplock_level = src->set_oplock_level;
	dst->create_lease_buf = src->create_lease_buf;
	dst->parse_lease_buf = src->parse_lease_buf;
	dst->copychunk_range = src->copychunk_range;
	dst->duplicate_extents = src->duplicate_extents;
	dst->validate_negotiate = NULL;
#ifdef CONFIG_CIFS_XATTR
	dst->query_all_EAs = src->query_all_EAs;
	dst->set_EA = src->set_EA;
#endif /* CIFS_XATTR */
	dst->wp_retry_size = src->wp_retry_size;
	dst->dir_needs_close = src->dir_needs_close;
	dst->select_sectype = src->select_sectype;
	dst->fallocate = src->fallocate;
	dst->get_acl = src->get_acl;
	dst->get_acl_by_fid = src->get_acl_by_fid;
	dst->set_acl = src->set_acl;
	dst->init_transform_rq = src->init_transform_rq;
	dst->is_transform_hdr = src->is_transform_hdr;
	dst->receive_transform = src->receive_transform;
	dst->make_node = src->make_node;
	dst->ioctl_query_info = src->ioctl_query_info;
	dst->fiemap = src->fiemap;
	dst->llseek = src->llseek;
	dst->is_status_io_timeout = src->is_status_io_timeout;
}

//	__u64 (*get_next_mid)(struct TCP_Server_Info *);
/**
 * This method smb1_operations and smb20_operations have different logic.
 * because the smb1 mid is only 16 bits. smb2 mid is 64 bits.
 * And smb1 negotiate can start with any mid.
 * smb2 negotiate must start with mid=0.
 * (Even if we start with 1, the server also reply 0)
 *
 * The origin implement:
 *   smb1 implement is silmilar to return ++CurrentMid
 *   smb2 return CurrentMid++
 *
 * So align the implement behavior for syno ops
 *
 */
static __u64
smb1_get_next_mid(struct TCP_Server_Info *server)
{
	__u64 mid = 0;
	__u16 last_mid, cur_mid;
	bool collision;

	spin_lock(&GlobalMid_Lock);

	/* mid is 16 bit only for CIFS/SMB */
	cur_mid = (__u16)((server->CurrentMid) & 0xffff);
	/* we do not want to loop forever */
	last_mid = cur_mid;
	/* avoid 0xFFFF MID */
	if (cur_mid == 0xffff)
		cur_mid++;

	/*
	 * This nested loop looks more expensive than it is.
	 * In practice the list of pending requests is short,
	 * fewer than 50, and the mids are likely to be unique
	 * on the first pass through the loop unless some request
	 * takes longer than the 64 thousand requests before it
	 * (and it would also have to have been a request that
	 * did not time out).
	 */
	do {
		struct mid_q_entry *mid_entry;
		unsigned int num_mids;

		collision = false;

		num_mids = 0;
		list_for_each_entry(mid_entry, &server->pending_mid_q, qhead) {
			++num_mids;
			if (mid_entry->mid == cur_mid &&
			    mid_entry->mid_state == MID_REQUEST_SUBMITTED) {
				/* This mid is in use, try a different one */
				collision = true;
				break;
			}
		}

		/*
		 * if we have more than 32k mids in the list, then something
		 * is very wrong. Possibly a local user is trying to DoS the
		 * box by issuing long-running calls and SIGKILL'ing them. If
		 * we get to 2^16 mids then we're in big trouble as this
		 * function could loop forever.
		 *
		 * Go ahead and assign out the mid in this situation, but force
		 * an eventual reconnect to clean out the pending_mid_q.
		 */
		if (num_mids > 32768)
			server->tcpStatus = CifsNeedReconnect;

		if (!collision) {
			mid = (__u64)cur_mid;
			server->CurrentMid = mid + 1;
			break;
		}
		cur_mid++;
	} while (cur_mid != last_mid);
	spin_unlock(&GlobalMid_Lock);
	return mid;
}
static __u64
syno_get_next_mid(struct TCP_Server_Info *server)
{
	if (SMB20_PROT_ID > server->dialect) {
		return smb1_get_next_mid(server);
	}
	return smb20_operations.get_next_mid(server);
}

//	struct mid_q_entry * (*find_mid)(struct TCP_Server_Info *, char *);
struct mid_q_entry *
syno_find_mid(struct TCP_Server_Info *server, char *buf)
{
	struct mid_q_entry *mid;
	struct smb2_sync_hdr *shdr = (struct smb2_sync_hdr *)buf;
	__u64 wire_mid = le64_to_cpu(shdr->MessageId);

	if (0xFF == (__u8)buf[4]) {
		return smb1_operations.find_mid(server, buf);
	}

	// smb2 part
	if (shdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM) {
		cifs_server_dbg(VFS, "Encrypted frame parsing not supported yet\n");
		return NULL;
	}

	spin_lock(&GlobalMid_Lock);
	list_for_each_entry(mid, &server->pending_mid_q, qhead) {
		if ((mid->mid == wire_mid) &&
		    (mid->mid_state == MID_REQUEST_SUBMITTED) &&
		    (mid->command == shdr->Command ||
		     //negotiate rqst might be come from SMB1
		     (mid->command == 0x72 && shdr->Command == 0)))
		{
			kref_get(&mid->refcount);
			spin_unlock(&GlobalMid_Lock);
			return mid;
		}
	}
	spin_unlock(&GlobalMid_Lock);
	return NULL;
}

//	int (*check_message)(char *, unsigned int, struct TCP_Server_Info *);
static int
syno_check_message(char *buf, unsigned int len, struct TCP_Server_Info *server)
{
	if (0xFF == (__u8)buf[4]) {
		return smb1_operations.check_message(buf, len, server);
	}
	// SMB2 header structure: 0xFE 'S' 'M' 'B'
	// if come from SMB1 nego -- 1st 4 bytes is netbios length and then 0xFF S M B
	if (8 < len && 'S' != buf[1] && 'S' == buf[5]) {
		return smb20_operations.check_message(buf+4, len - 4, server);
	}
	return smb20_operations.check_message(buf, len, server);
}

//	int (*negotiate)(const unsigned int, struct cifs_ses *);
static bool
should_set_ext_sec_flag(enum securityEnum sectype)
{
	switch (sectype) {
	case RawNTLMSSP:
	case Kerberos:
		return true;
	case Unspecified:
		if (global_secflags &
		    (CIFSSEC_MAY_KRB5 | CIFSSEC_MAY_NTLMSSP))
			return true;
		fallthrough;
	default:
		return false;
	}
}
static int
decode_ext_sec_blob(struct cifs_ses *ses, NEGOTIATE_RSP *pSMBr)
{
	int	rc = 0;
	u16	count;
	char	*guid = pSMBr->u.extended_response.GUID;
	struct TCP_Server_Info *server = ses->server;

	count = get_bcc(&pSMBr->hdr);
	if (count < SMB1_CLIENT_GUID_SIZE)
		return -EIO;

	spin_lock(&cifs_tcp_ses_lock);
	if (server->srv_count > 1) {
		spin_unlock(&cifs_tcp_ses_lock);
		if (memcmp(server->server_GUID, guid, SMB1_CLIENT_GUID_SIZE) != 0) {
			cifs_dbg(FYI, "server UID changed\n");
			memcpy(server->server_GUID, guid, SMB1_CLIENT_GUID_SIZE);
		}
	} else {
		spin_unlock(&cifs_tcp_ses_lock);
		memcpy(server->server_GUID, guid, SMB1_CLIENT_GUID_SIZE);
	}

	if (count == SMB1_CLIENT_GUID_SIZE) {
		server->sec_ntlmssp = true;
	} else {
		count -= SMB1_CLIENT_GUID_SIZE;
		rc = decode_negTokenInit(
			pSMBr->u.extended_response.SecurityBlob, count, server);
		if (rc != 1)
			return -EINVAL;
	}

	return 0;
}
static int
syno_check_smb1_nego_rsp(struct cifs_ses *ses, NEGOTIATE_RSP *pSMBr)
{
	int rc = 0;
	struct TCP_Server_Info *server = ses->server;

	rc = map_smb_to_linux_error((char *)&pSMBr->hdr, false);
	if (0 != rc) {
		goto neg_err_exit;
	}
	server->dialect = le16_to_cpu(pSMBr->DialectIndex);
	server->vals = &smb1_values;
	/* Check wct = 1 error case */
	if ((pSMBr->hdr.WordCount < 13) || (server->dialect == BAD_PROT)) {
		/* core returns wct = 1, but we do not ask for core - otherwise
		small wct just comes when dialect index is -1 indicating we
		could not negotiate a common dialect */
		rc = -EOPNOTSUPP;
		goto neg_err_exit;
	} else if (pSMBr->hdr.WordCount == 13) {
		server->negflavor = CIFS_NEGFLAVOR_LANMAN;
		rc = -EOPNOTSUPP;
		goto neg_err_exit;
	} else if (pSMBr->hdr.WordCount != 17) {
		/* unknown wct */
		rc = -EOPNOTSUPP;
		goto neg_err_exit;
	}
	/* else wct == 17, NTLM or better */

	server->sec_mode = pSMBr->SecurityMode;
	if ((server->sec_mode & SECMODE_USER) == 0)
		cifs_dbg(FYI, "share mode security\n");

	/* one byte, so no need to convert this or EncryptionKeyLen from
	   little endian */
	server->maxReq = min_t(unsigned int, le16_to_cpu(pSMBr->MaxMpxCount),
			       cifs_max_pending);
	set_credits(server, server->maxReq);
	/* probably no need to store and check maxvcs */
	server->maxBuf = le32_to_cpu(pSMBr->MaxBufferSize);
	/* set up max_read for readpages check */
	server->max_read = server->maxBuf;
	server->max_rw = le32_to_cpu(pSMBr->MaxRawSize);
	cifs_dbg(NOISY, "Max buf = %d\n", ses->server->maxBuf);
	server->capabilities = le32_to_cpu(pSMBr->Capabilities);
	server->timeAdj = (int)(__s16)le16_to_cpu(pSMBr->ServerTimeZone);
	server->timeAdj *= 60;

	if (pSMBr->EncryptionKeyLength == CIFS_CRYPTO_KEY_SIZE) {
		server->negflavor = CIFS_NEGFLAVOR_UNENCAP;
		memcpy(ses->server->cryptkey, pSMBr->u.EncryptionKey,
		       CIFS_CRYPTO_KEY_SIZE);
	} else if (pSMBr->hdr.Flags2 & SMBFLG2_EXT_SEC ||
			server->capabilities & CAP_EXTENDED_SECURITY) {
		server->negflavor = CIFS_NEGFLAVOR_EXTENDED;
		rc = decode_ext_sec_blob(ses, pSMBr);
	} else if (server->sec_mode & SECMODE_PW_ENCRYPT) {
		rc = -EIO; /* no crypt key only if plain text pwd */
	} else {
		server->negflavor = CIFS_NEGFLAVOR_UNENCAP;
		server->capabilities &= ~CAP_EXTENDED_SECURITY;
	}

	if (!rc)
		rc = cifs_enable_signing(server, ses->sign);
neg_err_exit:
	return rc;
}
static int
syno_check_smb2_nego_rsp(struct cifs_ses *ses, struct smb2_negotiate_rsp *rsp, size_t rsp_iov_len)
{
	int rc = 0;
	struct TCP_Server_Info *server = ses->server;
	int blob_offset, blob_length;
	char *security_blob;

	rc = map_smb2_to_linux_error((char *)&rsp->sync_hdr, false);
	if (0 != rc) {
		goto neg_exit;
	}
	/*
	 * No tcon so can't do
	 * cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_fail[SMB2...]);
	 */

	cifs_dbg(FYI, "mode 0x%x\n", rsp->SecurityMode);

	/* BB we may eventually want to match the negotiated vs. requested
	   dialect, even though we are only requesting one at a time */
	if (rsp->DialectRevision == cpu_to_le16(SMB20_PROT_ID)) {
		cifs_dbg(FYI, "negotiated smb2.0 dialect\n");
		server->vals = &smb20_values;
		// SMB2.02 may be come from SMB1 Nego.
		// So need to use syno function to process.
		// Don't set operations here
		// We set operation after this function.
	} else if (rsp->DialectRevision == cpu_to_le16(SMB21_PROT_ID)) {
		cifs_dbg(FYI, "negotiated smb2.1 dialect\n");
		server->vals = &smb21_values;
		set_operation(server->ops, &smb21_operations);
	} else if (rsp->DialectRevision == cpu_to_le16(SMB30_PROT_ID)) {
		cifs_dbg(FYI, "negotiated smb3.0 dialect\n");
		server->vals = &smb30_values;
		set_operation(server->ops, &smb30_operations);
	} else if (rsp->DialectRevision == cpu_to_le16(SMB302_PROT_ID)) {
		cifs_dbg(FYI, "negotiated smb3.02 dialect\n");
		server->vals = &smb302_values;
		set_operation(server->ops, &smb30_operations);
	} else if (rsp->DialectRevision == cpu_to_le16(SMB311_PROT_ID)) {
		cifs_dbg(FYI, "negotiated smb3.1.1 dialect\n");
		server->vals = &smb311_values;
		set_operation(server->ops, &smb311_operations);
	} else if (rsp->DialectRevision == cpu_to_le16(0x02ff)) {
		cifs_dbg(FYI, "negotiated smb2.FF dialect\n");
	} else {
		cifs_dbg(VFS, "Illegal dialect returned by server 0x%x\n",
			 le16_to_cpu(rsp->DialectRevision));
		rc = -EIO;
		goto neg_exit;
	}
	server->dialect = le16_to_cpu(rsp->DialectRevision);
	if (rsp->DialectRevision == cpu_to_le16(0x02ff)) {
		server->dialect = cpu_to_le16(SMB21_PROT_ID);
	}

	/*
	 * Keep a copy of the hash after negprot. This hash will be
	 * the starting hash value for all sessions made from this
	 * server.
	 */
	memcpy(server->preauth_sha_hash, ses->preauth_sha_hash,
	       SMB2_PREAUTH_HASH_SIZE);

	/* SMB2 only has an extended negflavor */
	server->negflavor = CIFS_NEGFLAVOR_EXTENDED;
	/* set it to the maximum buffer size value we can send with 1 credit */
	server->maxBuf = min_t(unsigned int, le32_to_cpu(rsp->MaxTransactSize),
			       SMB2_MAX_BUFFER_SIZE);
	server->max_read = le32_to_cpu(rsp->MaxReadSize);
	server->max_write = le32_to_cpu(rsp->MaxWriteSize);
	server->sec_mode = le16_to_cpu(rsp->SecurityMode);
	if ((server->sec_mode & SMB2_SEC_MODE_FLAGS_ALL) != server->sec_mode)
		cifs_dbg(FYI, "Server returned unexpected security mode 0x%x\n",
				server->sec_mode);
	server->capabilities = le32_to_cpu(rsp->Capabilities);
	/* Internal types */
	server->capabilities |= SMB2_NT_FIND | SMB2_LARGE_FILES;

	security_blob = smb2_get_data_area_len(&blob_offset, &blob_length,
					       (struct smb2_sync_hdr *)rsp);
	/*
	 * See MS-SMB2 section 2.2.4: if no blob, client picks default which
	 * for us will be
	 *	ses->sectype = RawNTLMSSP;
	 * but for time being this is our only auth choice so doesn't matter.
	 * We just found a server which sets blob length to zero expecting raw.
	 */
	if (blob_length == 0) {
		cifs_dbg(FYI, "missing security blob on negprot\n");
	}

	rc = cifs_enable_signing(server, ses->sign);
	if (rc) {
		goto neg_exit;
	}
	if (blob_length) {
		rc = decode_negTokenInit(security_blob, blob_length, server);
		if (rc == 1) {
			rc = 0;
		} else if (rc == 0) {
			rc = -EIO;
		}
	}

	if (rsp->DialectRevision == cpu_to_le16(SMB311_PROT_ID)) {
		if (rsp->NegotiateContextCount) {
			rc = smb311_decode_neg_context(rsp, server,
						       rsp_iov_len);
		} else {
			cifs_server_dbg(VFS, "Missing expected negotiate contexts\n");
		}
	}
neg_exit:
	return rc;
}

static int
small_smb1_nego_init(__le16 smb_command, void **request_buf)
{
	int rc = 0;

	/* BB eventually switch this to SMB2 specific small buf size */
	*request_buf = cifs_small_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}

	header_assemble((struct smb_hdr *) *request_buf, smb_command, NULL, 0);

	return rc;
}

static int
SYNO_negotiate_SMB1_start(const unsigned int xid, struct cifs_ses *ses)
{
	NEGOTIATE_REQ *pSMB;

	struct kvec rsp_iov[1];
	int rc = 0;
	int i;
	struct TCP_Server_Info *server = ses->server;
	char *buf = NULL;
	u16 count;
	__u8 *ubuf = NULL;

	if (!server) {
		WARN(1, "%s: server is NULL!\n", __func__);
		return -EIO;
	}
	server->dialect = 0;
	server->CurrentMid = 0;
	server->vals = &smb1_values;

	set_operation(server->ops, &smb1_operations);
	server->ops->check_message = syno_check_message;
	server->ops->next_header = syno_next_header;
	server->ops->get_next_mid = syno_get_next_mid;
	server->ops->find_mid = syno_find_mid;

	rc = small_smb1_nego_init(SMB_COM_NEGOTIATE, (void **) &pSMB);
	if (rc)
		return rc;
	rsp_iov[0].iov_base = cifs_small_buf_get();
	if (NULL == rsp_iov[0].iov_base) {
		rc = -ENOMEM;
		goto neg_err_exit;
	}

	pSMB->hdr.Mid = get_next_mid(server);
	pSMB->hdr.Flags2 |= (SMBFLG2_UNICODE | SMBFLG2_ERR_STATUS);

	if (should_set_ext_sec_flag(ses->sectype)) {
		cifs_dbg(FYI, "Requesting extended security\n");
		pSMB->hdr.Flags2 |= SMBFLG2_EXT_SEC;
	}

	count = 0;
	for (i = 0; NULL != smb1_dialects_array[i].name; i++) {
		strncpy(pSMB->DialectsArray+count, smb1_dialects_array[i].name, 16);
		count += strlen(smb1_dialects_array[i].name) + 1;
		/* null at end of source and target buffers anyway */
	}
	inc_rfc1001_len(pSMB, count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceiveSyno(xid, ses, (struct smb_hdr *) pSMB,
			 rsp_iov, 0);
	if (rc != 0) {
		goto neg_err_exit;
	}

	ubuf = (__u8 *)rsp_iov[0].iov_base;
	if (0xFF == ubuf[4]) {
		//check smb1 response.
		//extract from the cifssmb.c CIFSSMBNegotiate response process
		rc = syno_check_smb1_nego_rsp(ses, rsp_iov[0].iov_base);
		if (0 != rc) {
			//force dialect to SMB2.1 for retry SMB2 negotiate
			server->dialect = SMB21_PROT_ID;
		}
	} else {
		//check smb2 response
		buf = (char *)rsp_iov[0].iov_base;
		rc = syno_check_smb2_nego_rsp(ses, (struct smb2_negotiate_rsp *)(buf + 4), rsp_iov[0].iov_len - 4);
	}
neg_err_exit:
	cifs_small_buf_release(pSMB);
	cifs_small_buf_release(rsp_iov[0].iov_base);

	cifs_dbg(FYI, "negprot rc %d\n", rc);
	return rc;
}

static int
smb2_nego_init(__le16 smb2_command, void **request_buf,
		struct TCP_Server_Info *server,
		unsigned int *total_len)
{
	struct smb2_sync_pdu *spdu = NULL;

	/* BB eventually switch this to SMB2 specific small buf size */
	*request_buf = cifs_small_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}
	memset((*request_buf), 0, 256);
	spdu = (struct smb2_sync_pdu *)(*request_buf);
	/*
	 * smaller than SMALL_BUFFER_SIZE but bigger than fixed area of
	 * largest operations (Create)
	 */

	smb2_hdr_assemble(&spdu->sync_hdr, smb2_command, NULL, server);

	// SMB2_NEGOTIATE 36 from smb2_req_struct_sizes
	spdu->StructureSize2 = cpu_to_le16(36);
	*total_len = 36 + sizeof(struct smb2_sync_hdr);

	return 0;
}
static int
syno_SMB2_negotiate(const unsigned int xid, struct cifs_ses *ses)
{
	struct smb_rqst rqst;
	struct smb2_negotiate_req *req;
	struct smb2_negotiate_rsp *rsp;
	struct kvec iov[1];
	struct kvec rsp_iov[1];
	int i;
	u16 count;
	int rc = 0;
	int resp_buftype;
	struct TCP_Server_Info *server = ses->server;
	int flags = CIFS_NEG_OP;
	unsigned int total_len;

	if (!server) {
		cifs_dbg(VFS, "%s: server is NULL!\n", __func__);
		return -EIO;
	}
	server->vals = &smb311_values;
	if (SMB20_PROT_ID > server->dialect) {
		server->dialect = cpu_to_le16(SMB311_PROT_ID);
	}
	set_operation(server->ops, &smb311_operations);

	rc = smb2_nego_init(SMB2_NEGOTIATE, (void **) &req, server, &total_len);
	if (rc) {
		return rc;
	}

	req->sync_hdr.SessionId = 0;

	memset(server->preauth_sha_hash, 0, SMB2_PREAUTH_HASH_SIZE);
	memset(ses->preauth_sha_hash, 0, SMB2_PREAUTH_HASH_SIZE);

	count = 0;
	for (i = 0; BAD_PROT_ID != smb2_dialects_array[i]; i++) {
		req->Dialects[count] = cpu_to_le16(smb2_dialects_array[i]);
		count++;
		total_len += 2;
	}

	req->DialectCount = cpu_to_le16(count);

	/* only one of SMB2 signing flags may be set in SMB2 request */
	if (ses->sign)
		req->SecurityMode = cpu_to_le16(SMB2_NEGOTIATE_SIGNING_REQUIRED);
	else if (global_secflags & CIFSSEC_MAY_SIGN)
		req->SecurityMode = cpu_to_le16(SMB2_NEGOTIATE_SIGNING_ENABLED);
	else
		req->SecurityMode = 0;

	req->Capabilities = cpu_to_le32(ses->server->vals->req_capabilities);

	/* ClientGUID must be zero for SMB2.02 dialect */
	if (ses->server->vals->protocol_id == SMB20_PROT_ID)
		memset(req->ClientGUID, 0, SMB2_CLIENT_GUID_SIZE);
	else {
		memcpy(req->ClientGUID, server->client_guid,
			SMB2_CLIENT_GUID_SIZE);
		assemble_neg_contexts(req, server, &total_len);
	}
	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, rsp_iov);
	cifs_small_buf_release(req);

	rsp = (struct smb2_negotiate_rsp *)rsp_iov[0].iov_base;
	/*
	 * No tcon so can't do
	 * cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_fail[SMB2...]);
	 */
	if (rc != 0) {
		goto neg_exit;
	}

	rc = syno_check_smb2_nego_rsp(ses, (struct smb2_negotiate_rsp *)rsp_iov[0].iov_base, rsp_iov[0].iov_len);
neg_exit:
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}
static int
syno_negotiate(const unsigned int xid, struct cifs_ses *ses)
{
	int rc;
	rc = SYNO_negotiate_SMB1_start(xid, ses);
	if (0 != rc) {
		ses->server->CurrentMid = 0;
	} else if (SMB20_PROT_ID >= ses->server->dialect) {
		goto END;
	}
	rc = syno_SMB2_negotiate(xid, ses);
	/* BB we probably don't need to retry with modern servers */
END:
	if (rc == -EAGAIN) {
		rc = -EHOSTDOWN;
	}
	if (0 != rc) {
		//negotiate fail ==> reset need_neg
		ses->server->maxBuf = 0;
		ses->server->max_read = 0;
	}
	if (0 == rc) {
		// if the response come from SMB1 nego,
		// we need to set operations to replace syno nego function
		if (SMB20_PROT_ID > ses->server->dialect) {
			set_operation(ses->server->ops, &smb1_operations);
		} else if (SMB20_PROT_ID == ses->server->dialect) {
			set_operation(ses->server->ops, &smb20_operations);
		}
	}
	return rc;
}


//	int (*next_header)(char *);
static int
syno_next_header(char *buf)
{
	// caller: connect.c -- cifs_demultiplex_thread
	// return 0 will do not thing
	struct smb2_sync_hdr *hdr = (struct smb2_sync_hdr *)buf;
	struct smb2_transform_hdr *t_hdr = (struct smb2_transform_hdr *)buf;

	if (0xFF == (__u8)buf[4]) {
		// SMB1 no Next command. directly return 0
		return 0;
	}
	if (hdr->Command == SMB2_NEGOTIATE) {
		// SMB1 Nego to SMB2 Nego(0x00) will no Next command. directly return 0
		// SMB2 Nego never compound now.
		return 0;
	}
	if (hdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM) {
		return sizeof(struct smb2_transform_hdr) +
		  le32_to_cpu(t_hdr->OriginalMessageSize);
	}

	return le32_to_cpu(hdr->NextCommand);
}

void init_syno_operations(struct TCP_Server_Info *server, struct smb_vol *volume_info)
{
	if (&synocifs_values == volume_info->vals) {
		server->values = volume_info->vals;

		// &synocifs_operations only have some nego function.
		// we need to initialize other operation's function.
		server->ops = &server->operations;
		server->ops->negotiate = synocifs_operations.negotiate;
		set_operation(server->ops, &smb1_operations);
	}
}

struct smb_version_operations synocifs_operations = {
	.negotiate = syno_negotiate,

	// For negotiate only:
	.get_next_mid = syno_get_next_mid,
	.find_mid = syno_find_mid,
	.check_message = syno_check_message,
	.next_header = syno_next_header,
};

struct smb_version_values synocifs_values = {
	.version_string = SYNO_VERSION_STRING,
	.protocol_id = SMB21_PROT_ID,	//should set after negotiate
	.req_capabilities = 0, /* MBZ on negotiate req until SMB3 dialect */
	.large_lock_type = 0,
	.exclusive_lock_type = SMB2_LOCKFLAG_EXCLUSIVE_LOCK,
	.shared_lock_type = SMB2_LOCKFLAG_SHARED_LOCK,
	.unlock_lock_type = SMB2_LOCKFLAG_UNLOCK,
	.header_size = sizeof(struct smb2_sync_hdr),
	.header_preamble_size = 0,
	.max_header_size = MAX_SMB2_HDR_SIZE,
	.read_rsp_size = sizeof(struct smb2_read_rsp) - 1,
	.lock_cmd = SMB2_LOCK,
	.cap_unix = 0,
	.cap_nt_find = SMB2_NT_FIND,
	.cap_large_files = SMB2_LARGE_FILES,
	.signing_enabled = SMB2_NEGOTIATE_SIGNING_ENABLED | SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.signing_required = SMB2_NEGOTIATE_SIGNING_REQUIRED,
	.create_lease_size = sizeof(struct create_lease),
};
