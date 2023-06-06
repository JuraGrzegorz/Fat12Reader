#include "file_reader.h"

int main(void) {

    return 0;
}
struct disk_t* disk_open_from_file(const char* volume_file_name){
    if(volume_file_name==0){
        errno=EFAULT;
        return 0;
    }
    FILE *f= fopen(volume_file_name,"rb");
    if(f==0){
        errno=ENOENT;
        return 0;
    }
    struct disk_t *res= calloc(1,sizeof(struct disk_t));
    if(res==0){
        errno=ENOMEM;
        return 0;
    }
    res->file=f;
    return res;
}
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read){
    if(pdisk==0 || first_sector<0 || buffer==0 || sectors_to_read<1){
        return 0;
    }
    fseek(pdisk->file, first_sector*BYTES_PER_BLOCK, SEEK_SET);

    int res = fread(buffer, BYTES_PER_BLOCK, sectors_to_read, pdisk->file);

    if(res!=sectors_to_read){
        return -1;
    }
    return res;
}
int disk_close(struct disk_t* pdisk){
    if(pdisk==0){
        errno=EFAULT;
        return -1;
    }
    fclose(pdisk->file);
    free(pdisk);
    return 0;
}

struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector){
    if(pdisk==0 || first_sector!=0){
        errno=EFAULT;
        return 0;
    }
    struct volume_t * res= calloc(1,sizeof(struct volume_t));
    if(res==0){
        errno=ENOMEM;
        return 0;
    }
    res->disk=pdisk;
    res->super= calloc(1,sizeof(struct super_t));
    if(res->super==0){
        free(res);
        errno=ENOMEM;
        return 0;
    }
    int read_val=disk_read(pdisk,0,res->super,1);
    if(read_val!=1){
        free(res->super);
        free(res);
        errno=ENOMEM;
        return 0;
    }
    if(res->super->bytes_per_sector==0){
        free(res->super);
        free(res);
        return 0;
    }
    res->fat1_data= calloc(res->super->bytes_per_sector*res->super->size_of_fat,sizeof(uint8_t));
    res->fat2_data=calloc(res->super->bytes_per_sector*res->super->size_of_fat,sizeof(uint8_t));
    fseek(res->disk->file, res->super->size_of_reserved_area*res->super->bytes_per_sector, SEEK_SET);
    disk_read(res->disk,res->super->size_of_reserved_area,res->fat1_data,res->super->size_of_fat);
    disk_read(res->disk,res->super->size_of_reserved_area+res->super->size_of_fat,res->fat2_data,res->super->size_of_fat);
    if(memcmp(res->fat1_data,res->fat2_data,res->super->bytes_per_sector*res->super->size_of_fat)!=0){
        free(res->fat1_data);
        free(res->fat2_data);
        free(res->super);
        free(res);
        return 0;
    }

    uint32_t sectors_per_rootdir = (res->super->maximum_number_of_files * sizeof(struct fat_entry_t)) / res->super->bytes_per_sector;
    if ((res->super->maximum_number_of_files * sizeof(struct fat_entry_t)) % res->super->bytes_per_sector != 0)
        sectors_per_rootdir++;
    uint32_t volume_size = res->super->number_of_sectors == 0 ? res->super->number_of_sectors_in_filesystem : res->super->number_of_sectors;
    uint32_t user_size=volume_size - (res->super->number_of_fats * res->super->size_of_fat) - res->super->size_of_reserved_area - sectors_per_rootdir;
    uint32_t number_of_cluster=user_size/res->super->sectors_per_clusters;

    uint32_t rootdir_position = res->super->size_of_reserved_area + res->super->size_of_fat * res->super->number_of_fats;
    uint32_t cluster2_position= rootdir_position + sectors_per_rootdir;

    uint16_t *fat_data = malloc((number_of_cluster + 2) * sizeof(uint16_t));
    for(uint16_t i = 0, j = 0; i < number_of_cluster + 2; i += 2, j += 3) {
        uint8_t b1 = res->fat1_data[j];
        uint8_t b2 = res->fat1_data[j + 1];
        uint8_t b3 = res->fat1_data[j + 2];

        uint16_t c1 = ((b2 & 0x0F) << 8) | b1;
        uint16_t c2 = ((b2 & 0xF0) >> 4) | (b3 << 4);
        fat_data[i] = c1;
        fat_data[i + 1] = c2;
    }
    res->fat_data=fat_data;
    res->cluster2_position=cluster2_position;
    return res;
}
int fat_close(struct volume_t* pvolume){
    if(pvolume==0){
        return 0;
    }
    free(pvolume->fat2_data);
    free(pvolume->fat1_data);
    free((pvolume->super));
    free(pvolume->fat_data);
    free(pvolume);
    return 1;
}
struct file_t* file_open(struct volume_t* pvolume, const char* file_name){
    if(pvolume==0 || file_name==0){
        errno=EFAULT;
        return 0;
    }
    struct fat_entry_t *root= calloc(pvolume->super->maximum_number_of_files,sizeof(struct fat_entry_t));
    if(root==0){
        return 0;
    }
    struct file_t *res= calloc(1,sizeof(struct file_t));
    if(res==0){
        free(root);
        errno=ENOMEM;
        return 0;
    }

    res->disk=pvolume->disk;
    int file_name_size;
    for(file_name_size=0;file_name_size<8;file_name_size++){
        if(*(file_name+file_name_size)==0 || *(file_name+file_name_size)=='.'){
            break;
        }
    }

    disk_read(pvolume->disk,pvolume->super->size_of_reserved_area+pvolume->super->size_of_fat*2,root,pvolume->super->maximum_number_of_files*32/BYTES_PER_BLOCK);
    int i;
    for(i=0;i<pvolume->super->maximum_number_of_files;i++){
        if(*(root+i)->name==0){
            break;
        }
        if(memcmp((root+i)->name,file_name,file_name_size)==0){
            if(strlen(file_name)>(unsigned int )file_name_size){
                int file_extension=0;
                for(int j=file_name_size+1;j<11;j++){
                    if(*((root+i)->name+j)==32){
                        break;
                    }
                    file_extension++;
                }
                if(memcmp((file_name+file_name_size+1),(root+i)->name+8,file_extension)==0){
                    if((root+i)->size==0){
                        errno=EISDIR;
                        free(res);
                        free(root);
                        return 0;
                    }else{
                        memcpy(&(res->fat),(root+i),32);
                        break;
                    }
                }
            }else{
                if((root+i)->size==0){
                    errno=EISDIR;
                    free(res);
                    free(root);
                    return 0;
                }else{
                    memcpy(&(res->fat),(root+i),32);
                    break;
                }
            }
        }
    }
    res->fat_data=pvolume->fat_data;
    res->cluster2_position=pvolume->cluster2_position;
    res->sectors_per_cluster=pvolume->super->sectors_per_clusters;
    free(root);

    if(*(res->fat.name)==0){
        free(res);
        errno=ENOENT;
        return 0;
    }

    return res;
}
int file_close(struct file_t* stream){
    if(stream==0){
        return -1;
    }
    free(stream);
    return 0;
}
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream){
    if(ptr==0 || size<1 || nmemb<1 || stream==0){
        return -1;
    }

    uint32_t size_to_load=size*nmemb;
    if(nmemb>stream->fat.size){
        if(stream->position_read+size_to_load>nmemb){
            return 0;
        }
    }else{
        if(stream->position_read+size_to_load>stream->fat.size){
            return 0;
        }
    }

    int current_cluster = stream->fat.low_order_address_of_first_cluster;
    int counter = 0;

    while (1){
        if(current_cluster >= 0xFF8)
            break;

        current_cluster=stream->fat_data[current_cluster];
        counter++;
    }

    current_cluster = stream->fat.low_order_address_of_first_cluster;

    uint32_t content_offset = 0;
    uint8_t *tmp= calloc(stream->sectors_per_cluster*BYTES_PER_BLOCK*counter,sizeof(uint8_t));
    if(tmp==0){
        return -1;
    }
    while (1){
        if(current_cluster >= 0xFF8)
            break;

        uint32_t cluster_position=stream->cluster2_position+(current_cluster-2)*stream->sectors_per_cluster;
        disk_read(stream->disk,cluster_position,tmp+content_offset,stream->sectors_per_cluster);

        current_cluster=stream->fat_data[current_cluster];
        content_offset += stream->sectors_per_cluster*512;
    }
    int size_to_copy;
    if(size_to_load>stream->fat.size){
        size_to_copy=stream->fat.size;
    }else{
        size_to_copy=size_to_load;
    }

    memcpy((char *)ptr,tmp+stream->position_read,size_to_copy);
    stream->position_read+=size_to_copy;

    free(tmp);
    return size_to_copy/size;
}
int32_t file_seek(struct file_t* stream, int32_t offset, int whence){
    if(stream==0 ){
        errno=EFAULT;
        return -1;
    }
    if(whence==0){
        if(stream->position_read+offset>stream->fat.size){
            errno=ENXIO;
            return -1;
        }
        stream->position_read+=offset;
        return stream->position_read;
    }
    if(whence==1){
        if(stream->position_read+offset> stream->fat.size){
            errno=ENXIO;
            return -1;
        }
        stream->position_read+=offset;

        return stream->position_read;
    }
    if(whence==2){
        if((int64_t)stream->fat.size+offset<0){
            errno=ENXIO;
            return -1;
        }
        stream->position_read=(int32_t)stream->fat.size+offset;
        return stream->position_read;
    }
    return -1;
}
struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path){
    if(pvolume==0 || dir_path==0){
        errno=EFAULT;
        return 0;
    }
    if(strcmp(dir_path,"\\")!=0){
        errno=ENOENT;
        return 0;
    }
    struct fat_entry_t *root= calloc(pvolume->super->maximum_number_of_files,sizeof(struct fat_entry_t));
    if(root==0){
        return 0;
    }

    disk_read(pvolume->disk,pvolume->super->size_of_reserved_area+pvolume->super->size_of_fat*2,root,pvolume->super->maximum_number_of_files*32/BYTES_PER_BLOCK);
    int i;
    uint16_t size=0;

    for(i=0;i<pvolume->super->maximum_number_of_files;i++){
        if(*(root+i)->name==0){
            break;
        }
        if(*((root+i)->name)!=-27){
            size++;
        }
    }

    struct dir_t *res= calloc(1,sizeof(struct dir_t));
    if(res==0){
        free(root);
        errno=ENOMEM;
        return 0;
    }
    res->root_dic= calloc(size,sizeof(struct fat_entry_t));
    if(res->root_dic==0){
        free(res);
        free(root);
        errno=ENOMEM;
        return 0;
    }
    size=0;
    for(i=0;i<pvolume->super->maximum_number_of_files;i++){
        if(*(root+i)->name==0){
            break;
        }
        if(*((root+i)->name)!=-27){
            memcpy(res->root_dic+size,(root+i),32);
            size++;
        }
    }
    res->size=size;
    free(root);

    return res;
}
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry){
    if(pdir==0 || pentry==0){
        return -1;
    }
    if(pdir->position==pdir->size){
        return 1;
    }
    for(int i=0;i<13;i++){
       *(pentry->name+i)=0;
    }
    int name_len=0;
    for(name_len=0;name_len<8;name_len++){
        if(*((pdir->root_dic+pdir->position)->name+name_len)==32){
            break;
        }
    }
    strncat(pentry->name,(pdir->root_dic+pdir->position)->name,name_len);
    int extension_len=0;

    for(int i=8;i<11;i++){
        if(*((pdir->root_dic+pdir->position)->name+i)==32){
            break;
        }
        extension_len++;
    }
    if(extension_len!=0){
        strcat(pentry->name,".");
        strncat(pentry->name,(pdir->root_dic+pdir->position)->name+8,extension_len);
    }
    pentry->size=(pdir->root_dic+pdir->position)->size;
    pdir->position++;
    return 0;
}
int dir_close(struct dir_t* pdir){
    if(pdir==0){
        return 1;
    }
    free(pdir->root_dic);
    free(pdir);
    return 0;
}


