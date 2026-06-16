# GeoMark SSH Setup — geomark-handheld (Pi 5)

Perform these steps once on `geomark-handheld`. After this, SSH to both
`geomark-base` and `geomark-rover` requires no password and no fingerprint
confirmation.

---

## 1. Generate key pair

```bash
ssh-keygen -t ed25519 -C "geomark-handheld" -f ~/.ssh/id_ed25519 -N ""
```

Key saved to `~/.ssh/id_ed25519` and `~/.ssh/id_ed25519.pub`.

---

## 2. Fix known_hosts ownership (if needed)

If `~/.ssh/known_hosts` is owned by root:

```bash
sudo chown alex:alex ~/.ssh/known_hosts
chmod 600 ~/.ssh/known_hosts
chmod 700 ~/.ssh/
```

---

## 3. Copy key to geomark-base

Must be on home network (not rover AP):

```bash
sudo nmcli connection up home-wifi
ssh-keyscan -H geomark-base.local >> ~/.ssh/known_hosts
ssh-copy-id -i ~/.ssh/id_ed25519.pub pi@geomark-base.local
```

---

## 4. Copy key to geomark-rover

Must be connected to rover AP:

```bash
sudo nmcli connection up geomark-client
ssh-keyscan -H 192.168.10.1 >> ~/.ssh/known_hosts
ssh-copy-id -i ~/.ssh/id_ed25519.pub pi@192.168.10.1
```

---

## 5. Create ~/.ssh/config

```
Host geomark-base
    HostName geomark-base.local
    User pi
    IdentityFile ~/.ssh/id_ed25519
    StrictHostKeyChecking accept-new

Host geomark-rover
    HostName 192.168.10.1
    User pi
    IdentityFile ~/.ssh/id_ed25519
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
```

```bash
chmod 600 ~/.ssh/config
```

`geomark-base` uses `accept-new` — accepts new host keys once, warns on
unexpected changes. `geomark-rover` uses `no` + `/dev/null` because the
rover AP IP is fixed but the host key may appear new each session.

---

## 6. Verify

```bash
ssh geomark-base "echo base works"
ssh geomark-rover "echo rover works"
```

Both should return without any prompts.

---

## nmcli Profile Name Reference

The home WiFi profile was renamed from the netplan default for convenience:

```bash
sudo nmcli connection modify "netplan-wlan0-WiFi" connection.id "home-wifi"
```

Current profiles on `geomark-handheld`:

| Profile | Purpose |
|---------|---------|
| `home-wifi` | Home network — internet + geomark-base reachable |
| `geomark-client` | Rover AP — `192.168.10.1/24` |