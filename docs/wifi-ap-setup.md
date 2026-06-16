# GeoMark WiFi AP Setup — geomark-rover (Pi Zero 2 W)

Perform these steps once on `geomark-rover`. After this, the `geomark-rover`
WiFi AP starts automatically on boot at `192.168.10.1/24`.

> **Subnet note:** The rover AP uses `192.168.10.x`. Home network uses
> `10.0.0.x`. These must not overlap.

---

## 1. Install packages

```bash
sudo apt update
sudo apt install -y hostapd dnsmasq
sudo systemctl unmask hostapd
```

---

## 2. Assign static IP to wlan0

Edit `/etc/dhcpcd.conf`, append:

```
interface wlan0
static ip_address=192.168.10.1/24
nohook wpa_supplicant
```

---

## 3. Configure hostapd

Create `/etc/hostapd/hostapd.conf`:

```
interface=wlan0
driver=nl80211
ssid=geomark-rover
hw_mode=g
channel=6
wmm_enabled=0
macaddr_acl=0
auth_algs=1
wpa=2
wpa_passphrase=geomark2024
wpa_key_mgmt=WPA-PSK
wpa_pairwise=TKIP
rsn_pairwise=CCMP
```

Point the daemon at this file — edit `/etc/default/hostapd`:

```
DAEMON_CONF="/etc/hostapd/hostapd.conf"
```

---

## 4. Configure dnsmasq

```bash
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
```

Create new `/etc/dnsmasq.conf`:

```
interface=wlan0
dhcp-range=192.168.10.2,192.168.10.20,255.255.255.0,24h
```

---

## 5. Enable and start services

```bash
sudo systemctl enable hostapd
sudo systemctl enable dnsmasq
sudo systemctl start hostapd
sudo systemctl start dnsmasq
```

---

## 6. Verify from geomark-handheld

```bash
# Connect to rover AP
sudo nmcli connection up geomark-client

# Test reachability
ping 192.168.10.1

# SSH in
ssh geomark-rover
```

---

## nmcli Profiles on geomark-handheld

| Profile | SSID | Purpose |
|---------|------|---------|
| `home-wifi` | home network | internet, geomark-base reachable |
| `geomark-client` | `geomark-rover` | rover AP, stream client |

To toggle between them:

```bash
sudo nmcli connection up home-wifi
sudo nmcli connection up geomark-client   # brings down home-wifi automatically
```

---

## 7. Test stream end-to-end

On `geomark-rover`:

```bash
sudo build/host/geomark --mode rover
```

On `geomark-handheld` (after connecting to rover AP):

```bash
sudo build/host/geomark --mode ui --host 192.168.10.1
```

For local host testing (no hardware):

```bash
# Terminal 1 — simulates rover
sudo build/host/geomark --mode rover

# Terminal 2 — simulates handheld
sudo build/host/geomark --mode ui --host 127.0.0.1
```