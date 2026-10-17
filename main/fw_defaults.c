#include <string.h>
#include "cfg.h"
#include "fw_defaults.h"

/* Empty means unset here: string/list types accept it, the owner code
 * generates real values on top (device id, AP ssid) or treats it as
 * none (last network, NTP servers). */

static const char kDevice[] =
    "package device\n"
    "version 1\n"
    "#@field devid string maxlen=7 label=\"Device ID\"\n"
    "devid ''\n"
    "#@field hostname hostname label=\"Hostname\"\n"
    "hostname 'nnode-0000'\n";

/* NTP servers are three plain keys: a joined list would overflow the
 * 127-char value cap with long hostnames. */
static const char kTime[] =
    "package time\n"
    "version 1\n"
    "#@field tz string maxlen=64 label=\"Timezone\"\n"
    "tz 'UTC0'\n"
    "#@field zonename string maxlen=64 label=\"Zone name\"\n"
    "zonename ''\n"
    "#@field enabled bool label=\"NTP enabled\"\n"
    "enabled '1'\n"
    "#@field server1 string maxlen=63 label=\"NTP server 1\"\n"
    "server1 'pool.ntp.org'\n"
    "#@field server2 string maxlen=63 label=\"NTP server 2\"\n"
    "server2 ''\n"
    "#@field server3 string maxlen=63 label=\"NTP server 3\"\n"
    "server3 ''\n";

static const char kSta[] =
    "package sta\n"
    "version 1\n"
    "#@field count int min=0 max=8 label=\"Saved networks\"\n"
    "count '0'\n"
    "#@field last string maxlen=32 label=\"Last network\"\n"
    "last ''\n";

/* Eight fixed slots; load compacts them in memory, store normalizes. */
static const char kNets[] =
    "package nets\n"
    "version 1\n"
    "#@field ssid1 string maxlen=32 label=\"SSID 1\"\n"
    "ssid1 ''\n"
    "#@field pass1 string maxlen=63 label=\"Password 1\"\n"
    "pass1 ''\n"
    "#@field bssid1 string maxlen=17 label=\"BSSID 1\"\n"
    "bssid1 ''\n"
    "#@field auth1 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 1\"\n"
    "auth1 'auto'\n"
    "#@field ssid2 string maxlen=32 label=\"SSID 2\"\n"
    "ssid2 ''\n"
    "#@field pass2 string maxlen=63 label=\"Password 2\"\n"
    "pass2 ''\n"
    "#@field bssid2 string maxlen=17 label=\"BSSID 2\"\n"
    "bssid2 ''\n"
    "#@field auth2 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 2\"\n"
    "auth2 'auto'\n"
    "#@field ssid3 string maxlen=32 label=\"SSID 3\"\n"
    "ssid3 ''\n"
    "#@field pass3 string maxlen=63 label=\"Password 3\"\n"
    "pass3 ''\n"
    "#@field bssid3 string maxlen=17 label=\"BSSID 3\"\n"
    "bssid3 ''\n"
    "#@field auth3 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 3\"\n"
    "auth3 'auto'\n"
    "#@field ssid4 string maxlen=32 label=\"SSID 4\"\n"
    "ssid4 ''\n"
    "#@field pass4 string maxlen=63 label=\"Password 4\"\n"
    "pass4 ''\n"
    "#@field bssid4 string maxlen=17 label=\"BSSID 4\"\n"
    "bssid4 ''\n"
    "#@field auth4 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 4\"\n"
    "auth4 'auto'\n"
    "#@field ssid5 string maxlen=32 label=\"SSID 5\"\n"
    "ssid5 ''\n"
    "#@field pass5 string maxlen=63 label=\"Password 5\"\n"
    "pass5 ''\n"
    "#@field bssid5 string maxlen=17 label=\"BSSID 5\"\n"
    "bssid5 ''\n"
    "#@field auth5 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 5\"\n"
    "auth5 'auto'\n"
    "#@field ssid6 string maxlen=32 label=\"SSID 6\"\n"
    "ssid6 ''\n"
    "#@field pass6 string maxlen=63 label=\"Password 6\"\n"
    "pass6 ''\n"
    "#@field bssid6 string maxlen=17 label=\"BSSID 6\"\n"
    "bssid6 ''\n"
    "#@field auth6 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 6\"\n"
    "auth6 'auto'\n"
    "#@field ssid7 string maxlen=32 label=\"SSID 7\"\n"
    "ssid7 ''\n"
    "#@field pass7 string maxlen=63 label=\"Password 7\"\n"
    "pass7 ''\n"
    "#@field bssid7 string maxlen=17 label=\"BSSID 7\"\n"
    "bssid7 ''\n"
    "#@field auth7 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 7\"\n"
    "auth7 'auto'\n"
    "#@field ssid8 string maxlen=32 label=\"SSID 8\"\n"
    "ssid8 ''\n"
    "#@field pass8 string maxlen=63 label=\"Password 8\"\n"
    "pass8 ''\n"
    "#@field bssid8 string maxlen=17 label=\"BSSID 8\"\n"
    "bssid8 ''\n"
    "#@field auth8 enum vals=auto,open,wpa,wpa2,wpa3 label=\"Auth 8\"\n"
    "auth8 'auto'\n";

static const char kAp[] =
    "package ap\n"
    "version 1\n"
    "#@field ssid string maxlen=32 label=\"SSID\"\n"
    "ssid ''\n"
    "#@field auth enum vals=open,wpa2,wpa_wpa2 label=\"Auth\"\n"
    "auth 'open'\n"
    "#@field pass string maxlen=63 label=\"Password\"\n"
    "pass ''\n"
    "#@field mode enum vals=auto,always label=\"Mode\"\n"
    "mode 'auto'\n"
    "#@field nat bool label=\"NAT\"\n"
    "nat '0'\n";

/* Fixed slots for the same value-cap reason as NTP servers above. */
static const char kAutostart[] =
    "package autostart\n"
    "version 1\n"
    "#@field entry1 string maxlen=64 label=\"Program 1\"\n"
    "entry1 ''\n"
    "#@field entry2 string maxlen=64 label=\"Program 2\"\n"
    "entry2 ''\n"
    "#@field entry3 string maxlen=64 label=\"Program 3\"\n"
    "entry3 ''\n"
    "#@field entry4 string maxlen=64 label=\"Program 4\"\n"
    "entry4 ''\n"
    "#@field entry5 string maxlen=64 label=\"Program 5\"\n"
    "entry5 ''\n"
    "#@field entry6 string maxlen=64 label=\"Program 6\"\n"
    "entry6 ''\n"
    "#@field entry7 string maxlen=64 label=\"Program 7\"\n"
    "entry7 ''\n"
    "#@field entry8 string maxlen=64 label=\"Program 8\"\n"
    "entry8 ''\n";

const char *nnode_fw_defaults(const char *pkg)
{
    if (!pkg) {
        return NULL;
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_DEVICE) == 0) {
        return kDevice;
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_TIME) == 0) {
        return kTime;
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_STA) == 0) {
        return kSta;
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_NETS) == 0) {
        return kNets;
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_AP) == 0) {
        return kAp;
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_AUTOSTART) == 0) {
        return kAutostart;
    }
    return NULL;
}
