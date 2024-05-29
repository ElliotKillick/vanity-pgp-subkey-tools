# Vanity PGP Subkey Tools

This repo contains helpful tools for generating vanity PGP subkeys. The tools here were created for use with vanity PGP key finders like [VanityGPG](https://github.com/RedL0tus/VanityGPG).

## Tools

- [`get-compatible-pgp-subkeys` Program](#get-compatible-pgp-subkey-program)
  - This program reads a directory full of PGP keys to find the ones that have a creation timestamp compatible with a given primary key
    - OpenPGP v4 includes a PGP key's creation timestamp as part of its (vanity) fingerprint calculation, so a compatible subkey must have a creation timestamp greater than or equal to the primary key
  - A fast C program is necessary to sort through the potential tens of millions of vanity key candidates you generated with VanityGPG or likewise
- [GPG commands for adding a vanity subkey to your vanity primary key](#adding-a-vanity-subkey-to-your-vanity-primary-key-instructions)
- A [VanityGPG fork](https://github.com/ElliotKillick/VanityGPG-ESS) for generating vanity encryption subkeys
  - For generating Curve25519 encryption keys ([this may be integrated in the future](https://github.com/RedL0tus/VanityGPG/issues/5))
  - **Note:** This fork is pending release.

## `get-compatible-pgp-subkeys` Program

### Usage

```
Usage: ./get-compatible-pgp-subkeys <SOURCE_DIRECTORY> [<PRIMARY_PGP_KEY> <DESTINATION_DIRECTORY>]

Passing a source directory with no other arguments opens each PGP key and prints its creation
timestamp. Further specifying a primary PGP key and destination directory will move each PGP key if
its creation timestamp is equal to or greater than that of the primary PGP key.

Both raw and ASCII-armored PGP keys are supported.
```

### Compiling

You can build a `get-compatible-pgp-subkeys` binary by doing:

Debian: `sudo apt-get install pkg-config libglib2.0-dev`

Fedora: `sudo dnf install pkg-config glib2-devel`

```shell
make
```

### Performance

**Performance is excellent:**

1. No dynamic memory allocation (`malloc`) in the main program loop (w/ in-place base64 decoding)
2. Fully zero copy / pass-by-reference (no `memcpy`, `strcpy`, etc.)
3. For armored (base64) PGP keys, only the exact 4 bytes of base64 containing the PGP creation timestamp are decoded

These three performance wins allow us to quickly process a huge number of keys. Disk I/O is currently the bottleneck (as it should be).

### Code Quality

The program structure is easy to understand. Return values of standard library functions (e.g. malloc, fread, fopen, etc.) are always checked to ensure success. The most crucial parts of the code are split up into their own functions so we don't repeat ourselves (DRY principle). The code compiles warning-free (even on `-Wall`). Address sanitizer has been used to ensure there's no memory corruption or resource leak problems. Only standard C features are used (other than a dependency on the cross-platform GNOME GLib library) so this code is portable across Windows, Mac, Linux, the BSDs, Solaris, Android, iOS, a toaster, etc.

## Adding a Vanity Subkey to Your Vanity Primary Key Instructions

### Make Key Importable

This step is only applicable if you didn't specify a user ID `-u`/`--user-id` when generating keys with VanityGPG. To add a temporary user ID so you can import your new vanity PGP key:

```shell
gpg --output <GPG_PRIVATE_KEY_FILE> --dearmor <ASC_PRIVATE_KEY_FILE>
printf '\xb4\x04temp' >> <GPG_PRIVATE_KEY_FILE>
gpg --allow-non-selfsigned-uid --import <GPG_PRIVATE_KEY_FILE>
```

If this key will be your primary key, proceed to correct the user ID of the imported key with GPG by running `gpg --edit-key <KEY_FINGERPRINT>`, then using the `adduid`, `uid`, and `deluid` commands to add a new user ID and delete the old one. Also, use the `trust` command to trust your own primary key ultimately. Run the `save` command to complete changes and exit. If you will be turning this key into a subkey, then you don't need to correct the user ID.

### Attach Vanity Subkey

1. Import your vanity key (if it's not already imported): `gpg --import <KEY_FILE>`
2. List keys with details to collect the keygrip: `gpg --list-keys --with-keygrip`
  - Keygrip is a GPG implementation detail referring to the secret key material (these are part of what calculates a PGP key fingerprint)
3. Get the creation timestamp of your vanity key: `gpg --list-packets <KEY_FILE> | grep '^\sversion 4' | grep -oE 'created [0-9]+'`
4. Knowing the keygrip and creation timestamp of the vanity key (soon-to-be subkey), we can now replicate its vanity PGP fingerprint onto our vanity primary key: `gpg --expert --faked-system-time '<KEY_TIMESTAMP>!' --edit-key <PRIMARY_KEY_FINGERPRINT>`
  - The `!` after a timestamp tells GPG to keep the clock exactly at the given time (no ticking)

Good, now in the GPG shell we operate on your primary key:

1. Run command: `addkey`
2. Enter `13` to choose this option: `(13) Existing key`
3. GPG will request your keygrip, enter it: `Enter the keygrip:`
4. Continue through the remaining typical questions asked when adding a PGP key (typically stick with the defaults)
5. Run `save` to complete changes and exit
6. Verify your new vanity subkey: `gpg --list-secret-keys --with-keygrip --with-subkey-fingerprints`
7. Export the final product (including primary key and subkeys):
  - Public: `gpg --output final.asc --armor --export <PRIMARY_KEY_FINGERPRINT>`
  - Private: `gpg --output final-secret.asc --armor --export-secret-key <PRIMARY_KEY_FINGERPRINT>`

**You're done! Your vanity primary key is now adorned with a vanity subkey!**

Here's what my final PGP keyring looks like:

```shell
$ gpg --list-keys --with-subkey-fingerprints
/home/user/.gnupg/pubring.kbx
-----------------------------
pub   ed25519 2024-05-12 [SC]
      EEEE1AE12A5B322909EBDC5D2CEEA9CE5BD0EEEE
uid           [ultimate] Elliot Killick <contact@elliotkillick.com>
sub   ed25519 2024-05-22 [S]
      EEEE6403CE850791ECB1F8207C4ECB25B6B1C0DE
sub   ed25519 2024-05-21 [S]
      EEEE819EAB6A6D1ACB25416A1B212E0A541E2222
```

## License

MIT License - Copyright (C) 2024 Elliot Killick <contact@elliotkillick.com>
