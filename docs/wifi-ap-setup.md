# GeoMark WiFi AP Setup — Pi Zero 2 W (Pole-Top)

Perform these steps once on the Zero 2 W OS. After this, the
`geomark-rover` WiFi hotspot starts automatically on boot.

## 1. Install packages

```bash
sudo apt update
sudo apt install -y hostapd dnsmasq
sudo systemctl unmask hostapd
```

## 2. Assign static IP to wlan0

Edit `/etc/dhcpcd.conf`, add at the end:
interface wlan0

static ip_address=10.0.0.1/24

nohook wpa_supplicant

## 3. Configure hostapd

Create `/etc/hostapd/hostapd.conf`:
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

Point the daemon at this file — edit `/etc/default/hostapd`:
DAEMON_CONF="/etc/hostapd/hostapd.conf"

## 4. Configure dnsmasq

Back up the original and replace `/etc/dnsmasq.conf`:

```bash
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
```

Create new `/etc/dnsmasq.conf`:
interface=wlan0

dhcp-range=10.0.0.2,10.0.0.20,255.255.255.0,24h

## 5. Enable and start services

```bash
sudo systemctl enable hostapd
sudo systemctl enable dnsmasq
sudo systemctl start hostapd
sudo systemctl start dnsmasq
```

## 6. Verify

On the Pi 5, scan for networks:

```bash
sudo iwlist wlan0 scan | grep geomark-rover
```

Then connect and ping:

```bash
sudo wpa_passphrase geomark-rover geomark2024 | sudo tee /etc/wpa_supplicant/wpa_supplicant.conf
sudo wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf
sudo dhclient wlan0
ping 10.0.0.1
```

## 7. Test the stream end-to-end

On the Zero 2 W:
```bash
sudo build/pi-rover/geomark --mode rover
```

On the Pi 5:
```bash
sudo build/host/geomark --mode ui --host 10.0.0.1
```

For local host testing (no hardware):
```bash
# Terminal 1 (simulates rover pole-top)
sudo build/host/geomark --mode rover
# Terminal 2 (simulates handheld)
sudo build/host/geomark --mode ui --host 127.0.0.1
```