#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define BYTES_PER_BLOCK    512

struct disk_t{
    FILE *file;
};

struct super_t {
    char unused[3];
    char name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_clusters;
    uint16_t size_of_reserved_area;
    uint8_t number_of_fats;
    uint16_t maximum_number_of_files;
    uint16_t number_of_sectors;
    uint8_t media_type;
    uint16_t size_of_fat;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t number_of_sectors_before_partition;
    uint32_t number_of_sectors_in_filesystem;
    uint8_t drive_number;
    uint8_t unused_1;
    uint8_t boot_signature;
    uint32_t serial_number;
    char label[11];
    char type[8];
    uint8_t unused_2[448];
    uint16_t signature;
} __attribute__(( packed ));

struct volume_t {
    struct super_t* super;
    struct disk_t* disk;
    uint32_t cluster2_position;
    uint8_t *fat1_data;
    uint8_t *fat2_data;
    uint16_t *fat_data;

} __attribute__ (( packed ));

enum fat_attribute_t {
    FAT_ATTRIB_READONLY=0x01,
    FAT_ATTRIB_HIDDEN=0x02,
    FAT_ATTRIB_SYSTEM=0x04,
    FAT_ATTRIB_VOLUME=0x08,
    FAT_DIRECTORY = 0x10,
    FAT_ATTRIB_ARCHIVE = 0x20,
} __attribute__(( packed ));

struct date_t {
    uint16_t day: 5;
    uint16_t month: 4;
    uint16_t year: 7;
};

struct my_time_t {
    uint16_t seconds: 5;
    uint16_t minutes: 6;
    uint16_t hours: 5;
};

struct fat_entry_t {
    char name[11];
    enum fat_attribute_t attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    struct my_time_t creation_time;
    struct date_t creation_date;
    uint16_t access_date;
    uint16_t high_order_address_of_first_cluster;
    struct my_time_t modified_time;
    struct date_t modified_date;
    uint16_t low_order_address_of_first_cluster;
    uint32_t size;
} __attribute__(( packed ));

struct file_t{
    struct fat_entry_t fat;
    struct disk_t* disk;
    uint16_t *fat_data;
    uint32_t cluster2_position;
    uint8_t sectors_per_cluster;
    uint32_t position_read;
};
struct dir_t{
    struct fat_entry_t *root_dic;
    uint16_t position;
    uint16_t size;
};

struct dir_entry_t{
    char name[13];
    uint32_t size;
    char is_archived;
    char is_readonly;
    char is_system;
    char is_hidden;
    char is_directory;
};

struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);

struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);

struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);


