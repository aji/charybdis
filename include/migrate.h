/*
 * charybdis: An allegedly useful ircd
 * migrate.h: Definitions for live client migrations
 *
 * Copyright (C) 2016 Alex Iadicicco <https://github.com/aji>
 */

#ifndef INCLUDED_migrate_h
#define INCLUDED_migrate_h

/* LIVE CLIENT MIGRATION

   This is an implementation of IRCv3 live client migration functionality.
   Under this protocol, servers can transfer clients between them, with the
   appropriate cooperation from the client.

   Refer to the IRCv3 'migrate' extension documentation for more information
   on what this looks like from the user's perspective. From the network's
   perspective, the migration process works like this:

   1. Server OLD decides a client is to be migrated to server NEW (there are a
      number of reasons why this might happen, but none of them are relevant
      now). OLD sends 'MIGRATE START' to the client. When the client responds
      with 'MIGRATE OK', the migration process begins. 'MIGRATE OK' is the last
      message from the client that will be processed locally.

   2. OLD starts the handoff process by sending any local-only data it has
      about the client to NEW, such as client capabilities, monitor lists, etc.

   3. After this handoff is complete, OLD broadcasts a "flip" message that NEW
      is now responsible for the client. All servers acknowledge this message.
      OLD uses this information to decide if remote messages (like KICK or JOIN)
      should be sent to the still-connected client, or will be processed and
      buffered by NEW for the client when they reconnect. The exact details of
      this algorithm are important and described in further detail later.
      NEW begins buffering output for the client immediately upon receiving the
      "flip" message.

   4. Once OLD is satisfied with the acks it's received, it sends a 'MIGRATE
      PROCEED' to the client and closes the connection.

   5. Eventually the client connects to NEW to finish the migration. NEW drains
      any buffered output and the client continues to use the network as normal
      but from a new server.

   The "flip" step is rather delicate, since it must be performed in a way that
   ensures OLD does not process a message that NEW will also process. For
   example, if a PRIVMSG to a channel comes from a client on a remote server,
   OLD needs a way to know if NEW will see that message and buffer output for
   the client in response.

   Consider the following network. A client is connected to OLD and is being
   migrated to NEW:

              a        b   c   d--e f
              |        |   |   |    |
           g--h--OLD===X===Y===Z===NEW--i
              |   |    |   |        |
              j   k l--m   n     o--p--q

   Luckily for us, we only need to focus on OLD, NEW, and the nodes on the
   path between them, in this case X, Y, and Z. I've used a thicker line to
   make this path stand out, but the links themselves are not different from
   the others. For the purpose of this discussion, I'll call this path the
   "direct path" between OLD and NEW. It's worth pointing out that, since the
   network is a tree, there can only be one such direct path.

   Consider for a moment the properties of messages propagating through the
   tree. For starters, any message will be seen by one of the direct path nodes
   first, before the others. Note also that if a node sends a message A before
   a message B, that all other nodes will see A before B. These properties
   aren't particularly surprising, but they're worth calling out here because
   the algorithm we'll discuss relies heavily on them.

   When OLD processes a message, how does it know if it should produce output
   for a client it's in the process of migrating? How does it know if NEW has
   seen the "flip" message before or after the message being processed? First,
   OLD determines which direct path node saw the message first. Then, OLD
   checks if this node has acknowledged the "flip" yet. If OLD has an ack for
   that node, then it knows NEW will produce output. Otherwise, OLD knows that
   NEW won't have seen the "flip" by the time it receives the message in
   question, and so OLD should produce output for the client.

   Why does this work? Suppose, in our example network, that OLD determines Y
   is the node that saw a message M first. (Maybe Y generated the message, or
   maybe c or n generated the message, but Y is the direct path node that saw it
   first.) When Y sees the "flip", it will forward the "flip" toward NEW, and
   simultaneously send an ack back toward OLD. If Y sees M before the "flip",
   then NEW will also see 'M' before the "flip", and OLD will see M before it
   sees Y's ack. If Y sees the "flip" first, however, then NEW will see the
   "flip" before M, and OLD will see Y's ack before it sees M. This same line
   of reasoning applies to all nodes along the direct path, including OLD and
   NEW. We can assume these things about message ordering due to the fact that
   each individual link is a queue of messages.

                  OLD   MID   NEW
                   ╵     ╵     ╵
                  flip   a     ╵
                   ╵ ╲ ╱ ╵ ╲   ╵
                   ╵  ╳  ╵  ╲  ╵           Legend
                   ╵ ╱ ╲ ╵   ╲ ╵           flip = the flip message
                   a    flip   a           a, b = arbitrary messages
                   ╵   ╱ ╵ ╲   ╵           mid  = MID's ack
                   ╵  ╱  ╵  ╲  ╵           ack  = NEW's ack
                   ╵ ╱   ╵   ╲ ╵
                  mid    b    flip
                   ╵   ╱ ╵ ╲ ╱ ╵
                   ╵  ╱  ╵  ╳  ╵
                   ╵ ╱   ╵ ╱ ╲ ╵
                   b    ack    b
                   ╵   ╱ ╵     ╵
                   ╵  ╱  ╵     ╵
                   ╵ ╱   ╵     ╵
                  ack    ╵     ╵
                   ╵     ╵     ╵
                   V     V     V

   This diagram summarizes how messages are propagating through the spanning
   tree. In this case, X Y and Z have been combined under the single MID node,
   but the results are unchanged. (Try drawing a version of this graph with
   more nodes between OLD and NEW if you're not convinced!) Notice that the
   order OLD receives a, b, and MID's ack matches the order that NEW receives
   a, b, and the flip. Since a and b both started their traversal through the
   direct path nodes starting with MID, OLD can determine the order they are
   received by NEW relative to the flip by looking at the order it received
   them relative to MID's ack.

   Side note: It's *not* possible to just use the acknowledgement status of
   whatever server generated the message. Suppose (again, using our example
   network above) that OLD sends the "flip" and then immediately after receives
   a message from node h. If OLD forwards this message toward NEW, then NEW
   is guaranteed to see it after the "flip", and will buffer output for the
   client. However, OLD has not yet received any acknowledgement from h, so
   it would erroneously assume that *it* should be producing output for the
   client. Therefore, the client will see a duplicate of the message when it
   eventually connects to NEW. */

#include <stdbool.h>
#include <stdint.h>

struct Client;

struct Migration
{
	/* The user being migrated. This is always non-NULL */
	struct Client *client_p;

	/* The server the client is being migrated to. If a client is migrating
	   here, this will be &me. */
	struct Client *destination_p;

	/* The server furthest along the direct path from here to the
	   destination that has acknowledged the flip. This will initally be
	   &me and, at the end of the acknowledgement cycle, will be equal to
	   destination_p

	   This is a small optimization on the flip acknowledgement algorithm
	   described above, which is used to determine if we are responsible
	   for messaging the client, or if the destination will buffer the
	   message. Due to the way messages propagate through the server tree,
	   we only need to store the furthest ack, rather than the ack status
	   of every node. */
	struct Client *furthest_ack_p;

	/* The tokens for this migration. charybdis-migrate is lazy and just
	   uses a randomly-generated 32 bit integer for each token. */
	uint32_t resume_token;
	uint32_t confirm_token;
};

extern void init_migrate(void);

/* Finds a migration for a given resume token */
extern struct Migration *find_migration(uint32_t resume_token);

/* Finalizes a migration. client_p is the client the migration is attached to.
   migrant_p is the client that presented the migration's resume token during
   registration. migrant_p will be gutted and local connection information will
   be transferred to client_p. Any buffered output is sent immediately. */
extern void migration_resume(struct Client *client_p, struct Client *migrant_p);

/* During a migration, the client is, in a vague sense, connected to both the
   old and new servers. When the old and new server process the same message,
   it's important that they come to the same conclusion as to which of them will
   produce output for the client. This is important to ensure that the client
   does not miss or see duplicate messages when migrating. This function tests
   whether this server can assume that the other server will produce output for
   the client, given the current status of the migration. */
extern bool migrate_skip_output(struct Client *target_p, struct Client *source_p);

#endif // #ifndef INCLUDED_migrate_h
