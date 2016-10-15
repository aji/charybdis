/*
 *  ircd-ratbox: A slightly useful ircd.
 *  spy_trace_notice.c: Sends a notice when someone uses TRACE or LTRACE
 *
 *  Copyright (C) 2002 Hybrid Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 */
#include <ircd/stdinc.h>
#include <ircd/modules.h>
#include <ircd/hook.h>
#include <ircd/client.h>
#include <ircd/ircd.h>
#include <ircd/send.h>

using namespace ircd;

static const char spy_desc[] = "Sends a notice when someone uses TRACE or LTRACE";

void show_trace(hook_data_client *);

mapi_hfn_list_av1 trace_hfnlist[] = {
	{"doing_trace", (hookfn) show_trace},
	{NULL, NULL}
};

DECLARE_MODULE_AV2(trace_spy, NULL, NULL, NULL, NULL, trace_hfnlist, NULL, NULL, spy_desc);

void
show_trace(hook_data_client *data)
{
	if(data->target)
		sendto_realops_snomask(SNO_SPY, L_ALL,
				"trace requested by %s (%s@%s) [%s] on %s",
				data->client->name, data->client->username,
				data->client->host, data->client->servptr->name,
				data->target->name);
	else
		sendto_realops_snomask(SNO_SPY, L_ALL,
				"trace requested by %s (%s@%s) [%s]",
				data->client->name, data->client->username,
				data->client->host, data->client->servptr->name);
}