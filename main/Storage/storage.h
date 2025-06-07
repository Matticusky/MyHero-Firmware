#ifndef STORAGE_H
#define STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif
void mount_storage();
void get_base_path(char *path, size_t size);
void get_storage_info(size_t *total_size, size_t *used_size, size_t *free_size);
void print_storage_info();
#ifdef __cplusplus
}
#endif
#endif // STORAGE_H
