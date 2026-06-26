# Havel and HPKG Volumes Setup

This directory structure is intended to be used with Docker volumes to persist Havel scripts and HPKG packages.

## Directory Structure
- `data/havel_scripts/`: Mount this to `/root/scripts/` in the container.
- `data/hpkg_packages/`: Mount this to `/root/.hpkg/` in the container.

## Usage with Docker

To run the container with these volumes persisted locally:

```bash
docker run -it \
  -v "$(pwd)/data/havel_scripts:/root/scripts" \
  -v "$(pwd)/data/hpkg_packages:/root/.hpkg" \
  havel-lang run /root/scripts/your_script.hv
```

You can now drop `.hv` files into `data/havel_scripts/` on your host, and they will be immediately available inside the container.
