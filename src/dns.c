/*
 * dns.c -- handles:
 *   DNS resolve calls and events
 *   provides the code used by the bot if the DNS module is not loaded
 *   DNS Tcl commands
 *
 * $Id$
 */

#include "common.h"
#include "dccutil.h"
#include "main.h"
#include "net.h"
#include "misc.h"
#include "src/mod/dns.mod/dns.h"
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dns.h"

devent_t	*dns_events = NULL;


/*
 *   DCC functions
 */

void dcc_dnswait(int idx, char *buf, int len)
{
  /* Ignore anything now. */
}

void eof_dcc_dnswait(int idx)
{
  putlog(LOG_MISC, "*", "Lost connection while resolving hostname [%s/%d]",
	 iptostr(htonl(dcc[idx].addr)), dcc[idx].port);
  killsock(dcc[idx].sock);
  lostdcc(idx);
}

static void display_dcc_dnswait(int idx, char *buf)
{
  sprintf(buf, "dns   waited %lis", now - dcc[idx].timeval);
}

static void kill_dcc_dnswait(int idx, void *x)
{
  register struct dns_info *p = (struct dns_info *) x;

  if (p) {
    if (p->host)
      free(p->host);
    if (p->cbuf)
      free(p->cbuf);
    free(p);
  }
}

struct dcc_table DCC_DNSWAIT =
{
  "DNSWAIT",
  DCT_VALIDIDX,
  eof_dcc_dnswait,
  dcc_dnswait,
  NULL,
  NULL,
  display_dcc_dnswait,
  kill_dcc_dnswait,
  NULL,
  NULL
};


/*
 *   DCC events
 */

/* Walk through every dcc entry and look for waiting DNS requests
 * of RES_HOSTBYIP for our IP address.
 */
static void dns_dcchostbyip(in_addr_t ip, char *hostn, int ok, void *other)
{
  for (int idx = 0; idx < dcc_total; idx++) {
    if ((dcc[idx].type == &DCC_DNSWAIT) &&
        (dcc[idx].u.dns->dns_type == RES_HOSTBYIP) &&
        (dcc[idx].u.dns->ip == ip)) {
      if (dcc[idx].u.dns->host)
        free(dcc[idx].u.dns->host);
      dcc[idx].u.dns->host = (char *) calloc(1, strlen(hostn) + 1);
      strcpy(dcc[idx].u.dns->host, hostn);
      if (ok)
        dcc[idx].u.dns->dns_success(idx);
      else
        dcc[idx].u.dns->dns_failure(idx);
    }
  }
}

/* Walk through every dcc entry and look for waiting DNS requests
 * of RES_IPBYHOST for our hostname.
 */
static void dns_dccipbyhost(in_addr_t ip, char *hostn, int ok, void *other)
{
  for (int idx = 0; idx < dcc_total; idx++) {
    if ((dcc[idx].type == &DCC_DNSWAIT) &&
        (dcc[idx].u.dns->dns_type == RES_IPBYHOST) &&
        !egg_strcasecmp(dcc[idx].u.dns->host, hostn)) {
      dcc[idx].u.dns->ip = ip;
      if (ok)
        dcc[idx].u.dns->dns_success(idx);
      else
        dcc[idx].u.dns->dns_failure(idx);
    }
  }
}

devent_type DNS_DCCEVENT_HOSTBYIP = {
  "DCCEVENT_HOSTBYIP",
  dns_dcchostbyip
};

devent_type DNS_DCCEVENT_IPBYHOST = {
  "DCCEVENT_IPBYHOST",
  dns_dccipbyhost
};

void dcc_dnsipbyhost(char *hostn)
{
  devent_t *de = NULL;

  for (de = dns_events; de; de = de->next) {
    if (de->type && (de->type == &DNS_DCCEVENT_IPBYHOST) &&
	(de->lookup == RES_IPBYHOST)) {
      if (de->res_data.hostname &&
	  !egg_strcasecmp(de->res_data.hostname, hostn))
	/* No need to add anymore. */
	return;
    }
  }

  de = (devent_t *) calloc(1, sizeof(devent_t));

  /* Link into list. */
  de->next = dns_events;
  dns_events = de;

  de->type = &DNS_DCCEVENT_IPBYHOST;
  de->lookup = RES_IPBYHOST;
  de->res_data.hostname = strdup(hostn);

  /* Send request. */
  dns_ipbyhost(hostn);
}

void dcc_dnshostbyip(in_addr_t ip)
{
  devent_t *de = NULL;

  for (de = dns_events; de; de = de->next) {
    if (de->type && (de->type == &DNS_DCCEVENT_HOSTBYIP) &&
	(de->lookup == RES_HOSTBYIP)) {
      if (de->res_data.ip_addr == ip)
	/* No need to add anymore. */
	return;
    }
  }

  de = (devent_t *) calloc(1, sizeof(devent_t));

  /* Link into list. */
  de->next = dns_events;
  dns_events = de;

  de->type = &DNS_DCCEVENT_HOSTBYIP;
  de->lookup = RES_HOSTBYIP;
  de->res_data.ip_addr = ip;

  /* Send request. */
  dns_hostbyip(ip);
}



/*
 *    Event functions
 */

void call_hostbyip(in_addr_t ip, char *hostn, int ok)
{
  devent_t *de = dns_events, *ode = NULL, *nde = NULL;

  while (de) {
    nde = de->next;
    if ((de->lookup == RES_HOSTBYIP) &&
	(!de->res_data.ip_addr || (de->res_data.ip_addr == ip))) {
      /* Remove the event from the list here, to avoid conflicts if one of
       * the event handlers re-adds another event. */
      if (ode)
	ode->next = de->next;
      else
	dns_events = de->next;

      if (de->type && de->type->event)
	de->type->event(ip, hostn, ok, de->other);
      else
	putlog(LOG_MISC, "*", "(!) Unknown DNS event type found: %s",
	       (de->type && de->type->name) ? de->type->name : "<empty>");
      free(de);
      de = ode;
    }
    ode = de;
    de = nde;
  }
}

void call_ipbyhost(char *hostn, in_addr_t ip, int ok)
{
  devent_t *de = dns_events, *ode = NULL, *nde = NULL;

  while (de) {
    nde = de->next;
    if ((de->lookup == RES_IPBYHOST) &&
	(!de->res_data.hostname ||
	 !egg_strcasecmp(de->res_data.hostname, hostn))) {
      /* Remove the event from the list here, to avoid conflicts if one of
       * the event handlers re-adds another event. */
      if (ode)
	ode->next = de->next;
      else
	dns_events = de->next;

      if (de->type && de->type->event)
	de->type->event(ip, hostn, ok, de->other);
      else
	putlog(LOG_MISC, "*", "(!) Unknown DNS event type found: %s",
	       (de->type && de->type->name) ? de->type->name : "<empty>");

      if (de->res_data.hostname)
	free(de->res_data.hostname);
      free(de);
      de = ode;
    }
    ode = de;
    de = nde;
  }
}
