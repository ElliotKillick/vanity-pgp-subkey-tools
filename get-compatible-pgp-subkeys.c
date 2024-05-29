// Copyright (C) 2024 Elliot Killick <contact@elliotkillick.com>
// Licensed under the MIT License. See LICENSE file for details.

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
// For GNOME library base64 decoder
// This base64 library was chosen because it's written in C (not C++), LGPL, has clean and relatively simple code, is well-tested, supports in-place decoding, and performs very well in performance tests
// There are other base64 libraries that support SSE/AVX decoding but that's only faster if you're decoding long base64 strings. This isn't the case for us because we're only decoding a few bytes at a time
#include <glib.h>

int pgp_key_raw_extract_timestamp(FILE* file, unsigned int* out_timestamp)
{
    if (fseek(file, 3, SEEK_SET) == 0) {
        if (fread(out_timestamp, 1, sizeof(*out_timestamp), file) < sizeof(*out_timestamp))
            return 1;
    }
    else
        return 1;

    return 0;
}

int pgp_key_dearmor_extract_timestamp(FILE* file, unsigned int* out_timestamp) {
    // 64 (length of ASCII key line + 1 (newline) + 1 (validate string isn't too long) + 1 (null byte)
    char line[67];
    char* timestamp_base64_pos;
    gint state = 0;
    guint save = 0;

    while(fgets(line, sizeof(line), file)) {
        // Ignore:
        // -----BEGIN/END PGP PRIVATE/PUBLIC KEY BLOCK-----
        // AND
        // "Comment:", "Version:", etc.
        if (strchr(line, '-') || strchr(line, ':')) {
            continue;
        }

        // Search for line with length of 64 (+1 for newline)
        // For GnuPG and Sequoia PGP at least, this is the length for one line of an ASCII-armored PGP key
        if (strlen(line) != sizeof(line) - 2) {
            continue;
        }

        // Seek to timestamp in base64 encoded file
        // 4 base64 characters = 3 output bytes (when decoded)
        timestamp_base64_pos = line + 4;

        // We only decode the next 6 + 2 (for "==" padding) base64 characters (4 output bytes when decoded)
        // We need the "==" padding after this base64 string for strict compliance to the base64 standard
        //   - Base64 output bytes must be a multiple of 3 otherwise padding is required (we have 4 output bytes which is why we need two "=" pads)
        //   - https://stackoverflow.com/a/36571117
        // Some base64 decoders may be permissive here (e.g. the "base64" CLI with a warning) but the glib decoder is not
        // This is faster because we only decode the necessary base64 to get our timestamp
        timestamp_base64_pos[6] = '=';
        timestamp_base64_pos[7] = '=';
        timestamp_base64_pos[8] = '\0';

        // Use "step" function to avoid malloc in non-step variant (we do our own memory allocation)
        // Passed in base64 string *must* be zero-terminated
        // Returns output length but we ignore it because we know that it's always going to be 4 for our input
        g_base64_decode_step(timestamp_base64_pos, strlen(timestamp_base64_pos), (unsigned char*)out_timestamp, &state, &save);

        return 0;
    }

    return 1;
}

int pgp_key_extract_timestamp(char* file_path, unsigned int* out_timestamp) {
    FILE* file;
    char key_armor_check_buf[5];

    file = fopen(file_path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open PGP key: %s\n", file_path);
        return 1;
    }

    // Search for start of:
    // -----BEGIN PGP PRIVATE/PUBLIC KEY BLOCK-----
    // If we find it then we must first dearmor the key
    if (fread(key_armor_check_buf, 1, sizeof(key_armor_check_buf), file) < sizeof(key_armor_check_buf)) {
        fprintf(stderr, "Failed to read PGP key: %s\n", file_path);
        fclose(file);
        return 1;
    }

    if (memcmp(key_armor_check_buf, "-----", sizeof(key_armor_check_buf)) != 0) {
        if (pgp_key_raw_extract_timestamp(file, out_timestamp) != 0) {
            fprintf(stderr, "Failed to extract timestamp from raw PGP key: %s\n", file_path);
            fclose(file);
            return 1;
        }
    }
    else {
        if (pgp_key_dearmor_extract_timestamp(file, out_timestamp) != 0) {
            fprintf(stderr, "Failed to dearmor and extract timestamp from PGP key: %s\n", file_path);
            fclose(file);
            return 1;
        }
    }

    // File format is in Network Byte Order (big endian) so convert it to our CPU endianness if necessary
    *out_timestamp = ntohl(*out_timestamp);

    fclose(file);
    return 0;
}

int main(int argc, char** argv)
{
    char* source_dir;
    char* dest_dir = NULL;
    unsigned int timestamp_query;
    char* primary_pgp_key_file_path;
    char* dest_file_path = NULL;
    size_t dest_file_path_len = 0;
    DIR* dir;
    struct dirent *dirent;
    char* file_path;
    size_t file_path_len;
    // We pass this value by reference to pgp_key_extract_timestamp
    // This is *much* faster than allocating new memory (malloc) on every loop
    unsigned int timestamp;

    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Usage: %s <SOURCE_DIRECTORY> [<PRIMARY_PGP_KEY> <DESTINATION_DIRECTORY>]\n\n"

                        "Passing a source directory with no other arguments opens each PGP key and prints its creation\n"
                        "timestamp. Further specifying a primary PGP key and destination directory will move each PGP key if\n"
                        "its creation timestamp is equal to or greater than that of the primary PGP key.\n\n"

                        "Both raw and ASCII-armored PGP keys are supported.\n", argv[0]);
        return 1;
    }

    source_dir = argv[1];

    if (argc == 4) {
        primary_pgp_key_file_path = argv[2];
        if (pgp_key_extract_timestamp(primary_pgp_key_file_path, &timestamp_query) != 0) {
                fprintf(stderr, "Failed to read from primary PGP key!\n");
                return 1;
        }
        fprintf(stderr, "Primary PGP key timestamp: %u\n", timestamp_query);

        dest_dir = argv[3];

        dest_file_path_len = strlen(dest_dir) + NAME_MAX + 1;
        dest_file_path = malloc(dest_file_path_len);
        if (dest_file_path == NULL) {
            fprintf(stderr, "Failed to allocate destination file path\n");
            return 1;
        }
    }

    if ((dir = opendir(source_dir)) == NULL) {
        fprintf(stderr, "Can't open directory %s\n", source_dir);
        return 1;
    }

    file_path_len = strlen(source_dir) + NAME_MAX + 1;
    file_path = malloc(file_path_len);
    if (file_path == NULL) {
        fprintf(stderr, "Failed to allocate file path\n");
        return 1;
    }

    while ((dirent = readdir(dir)) != NULL) {
        struct stat stbuf;
        snprintf(file_path, file_path_len, "%s/%s", source_dir, dirent->d_name);
        if (stat(file_path, &stbuf) == -1) {
            fprintf(stderr, "Unable to stat file: %s\n", file_path);
            continue;
        }

        // Skip directories
        if ((stbuf.st_mode & S_IFMT) == S_IFDIR)
            continue;

        // Skip empty files
        // This can happen if VanityGPG exits abruptly before writing key contents to a created file
        if (stbuf.st_size == 0)
            continue;

        // Skip hidden files
        if (dirent->d_name[0] == '.')
            continue;

        fprintf(stderr, "Opening: %s\n", file_path);
        if (pgp_key_extract_timestamp(file_path, &timestamp) != 0)
            continue;
        printf("Timestamp: %u\n", timestamp);

        if (argc == 4) {
            // Testing with GPG confirms that a subkey with the same timestamp as its primary key is also valid
            if (timestamp >= timestamp_query) {
                fprintf(stderr, "Moving compatible PGP subkey: %s\n", file_path);
                snprintf(dest_file_path, dest_file_path_len, "%s/%s", dest_dir, dirent->d_name);
                if (rename(file_path, dest_file_path) == -1) {
                    fprintf(stderr, "Failed to move PGP key file: %s\n", file_path);
                }
            }
        }
    }

    free(file_path);
    closedir(dir);
    if (argc == 4)
        free(dest_file_path);
}
