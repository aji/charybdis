/*
 * charybdis-migrate: an ircd of sorts
 * migrate.c: Live client migrations
 *
 * Copyright (C) 2016 Alex Iadicicco
 */

#include <stdint.h>

#include "stdinc.h"
#include "defaults.h"

#include "client.h"
#include "migrate.h"

void init_migrate(void)
{
	// TODO MIGRATE: create migration dictionary
}

struct Migration *find_migration(uint32_t resume_token)
{
	// TODO MIGRATE
	return NULL;
}

void migration_resume(struct Client *client_p, struct Client *migrant_p)
{
	if (!client_p || !migrant_p || !client_p->migration) {
		// TODO MIGRATE: LOGIC ERROR
		// Tried to migrate a client that was missing a migration
		return;
	}

	// TODO MIGRATE: lol this
}

bool migrate_skip_output(struct Client *target_p, struct Client *source_p)
{
	struct Migration *mig;
	struct Client *next_ack_p;
	struct Client *cursor_p;

	/* Comments in this function use "pseudo-local" and "pseudo-remote" to
	   mean that output should either be produced or not. That is, we should
	   not produce output for a pseudo-remote client, even if we have an
	   active local connection or local line buffer for that client. You can
	   pretend migrate_skip_output actually means migrate_is_pseudo_remote,
	   but the current name was chosen to make call sites easier to follow,
	   and to emphasize the fact that the result of this function is really
	   only relevant when processing a single message from a remote server. */

	/* Messages for non-migrating clients are not affected. */
	if (!target_p || !source_p || !target_p->migration) {
		return false;
	}

	mig = target_p->migration;

	/* Clients migrating here are only pseudo-remote if we've not yet flipped
	   to taking responsibility for that client. If we're the destination,
	   we'll only call this function when we already know the client is
	   local (i.e. have already processed the flip) but we check MyConnect
	   anyway, just in case. */
	if (mig->destination_p == &me) {
		return !MyConnect(target_p);
	}

	/* If we haven't even sent out the flip yet, then the client is
	   definitely pseudo-local */
	if (!mig->furthest_ack_p) {
		return false;
	}

	/* Clients migrating away from us are always pseudo-remote if the
	   destination has acked the flip. */
	if (mig->furthest_ack_p == mig->destination_p) {
		return true;
	}

	/* At this point, we're concerned with the nodes between &me and the
	   destination (inclusive), and know that at least one of these nodes has
	   not yet acked the flip. Our goal is to find the node on this path that
	   will see the message from source_p first and determine if they've
	   acked the flip. If they have, we know that destination_p will see the
	   flip first, and we should treat the client as pseudo-remote. Otherwise,
	   the destination will see the message first, and we should treat the
	   client as pseudo-local.

	   To do this efficiently, we first find the closest node between the
	   destination and here that has yet to ack the flip. Then, we check if
	   this node is on the path from source_p to here:

	        ME---A---B---C---DEST (mig->destination_p)
	                  \
	                   X (source_p)

	   If A is the next node to ack: DEST sees the message before the flip,
	   client is pseudo-local.

	   If B is the next node to ack: DEST sees the message before the flip,
	   client is pseudo-local.

	   If C is the next node to ack: DEST sees the flip before the message,
	   client is pseudo-remote.

	   In other words, if our next ack node is on the path from source_p to
	   &me, then the client is pseudo-local. Note that this "proof" can
	   be generalized to any number of nodes between ME and DEST, and also
	   covers the case where DEST is the next node to ack. (ME cannot be
	   the next node to ack at this point.) */

	/* Step 1: Find the next node to ack the flip. */
	for (next_ack_p = mig->destination_p; ; next_ack_p = next_ack_p->servptr) {
		if (next_ack_p == &me) {
			// TODO MIGRATE: LOGIC ERROR
			// Can only happen if furthest_ack_p is not &me or one
			// of the nodes between &me and destination_p (exclusive)
			return false;
		}

		if (next_ack_p->servptr == mig->furthest_ack_p) {
			break;
		}
	}

	/* Step 2: Check if next_ack_p is on the path from source_p to &me. */
	for (cursor_p = source_p; cursor_p != &me; cursor_p = cursor_p->servptr) {
		if (cursor_p == next_ack_p) {
			return false;
		}
	}

	/* next_ack_p is not on the path from source_p to &me, so the node that
	   eventually forwards the message to destination_p will do so after
	   forwarding the flip, and the client is pseudo-remote */
	return true;
}
