#include "nat64/mod/common/nl/session.h"

#include "nat64/mod/stateful/session/db.h"
#include "nat64/mod/common/nl/nl_common.h"
#include "nat64/mod/common/nl/nl_core2.h"

static int session_entry_to_userspace(struct session_entry *entry, void *arg)
{
	struct nlcore_buffer *buffer = (struct nlcore_buffer *) arg;
	struct session_entry_usr entry_usr;
	unsigned long dying_time;

	if (!entry->expirer)
		return -EINVAL;

	entry_usr.src6 = entry->src6;
	entry_usr.dst6 = entry->dst6;
	entry_usr.src4 = entry->src4;
	entry_usr.dst4 = entry->dst4;
	entry_usr.state = entry->state;

	dying_time = entry->update_time + entry->expirer->timeout;
	entry_usr.dying_time = (dying_time > jiffies)
			? jiffies_to_msecs(dying_time - jiffies)
			: 0;

	return nlbuffer_write(buffer, &entry_usr, sizeof(entry_usr));
}

static int handle_session_display(struct sessiondb *db, struct genl_info *info,
		struct request_session *request)
{
	struct nlcore_buffer buffer;
	struct ipv4_transport_addr *remote4 = NULL;
	struct ipv4_transport_addr *local4 = NULL;
	int error;

	if (verify_superpriv())
		return nlcore_respond(info, -EPERM);

	log_debug("Sending session table to userspace.");

	error = nlbuffer_init_response(&buffer, info, nlbuffer_response_max_size());
	if (error)
		return nlcore_respond(info, error);

	if (request->display.connection_set) {
		remote4 = &request->display.remote4;
		local4 = &request->display.local4;
	}

	error = sessiondb_foreach(db, request->l4_proto,
			session_entry_to_userspace, &buffer,
			remote4, local4, false);
	nlbuffer_set_pending_data(&buffer, error > 0);
	error = (error >= 0)
			? nlbuffer_send(info, &buffer)
			: nlcore_respond(info, error);

	nlbuffer_free(&buffer);
	return error;
}

static int handle_session_count(struct sessiondb *db, struct genl_info *info,
		struct request_session *request)
{
	int error;
	__u64 count;

	log_debug("Returning session count.");

	error = sessiondb_count(db, request->l4_proto, &count);
	if (error)
		return nlcore_respond(info, error);

	return nlcore_respond_struct(info, &count, sizeof(count));
}

int handle_session_config(struct xlator *jool, struct genl_info *info)
{
	struct request_hdr *hdr;
	struct request_session *request;
	int error;

	if (xlat_is_siit()) {
		log_err("SIIT doesn't have session tables.");
		return nlcore_respond(info, -EINVAL);
	}

	hdr = get_jool_hdr(info);
	request = (struct request_session *)(hdr + 1);

	error = validate_request_size(info, sizeof(*request));
	if (error)
		return nlcore_respond(info, error);

	switch (be16_to_cpu(hdr->operation)) {
	case OP_DISPLAY:
		return handle_session_display(jool->nat64.session, info, request);
	case OP_COUNT:
		return handle_session_count(jool->nat64.session, info, request);
	}

	log_err("Unknown operation: %u", be16_to_cpu(hdr->operation));
	return nlcore_respond(info, -EINVAL);
}
