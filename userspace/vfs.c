/*
    RExOS - embedded RTOS
    Copyright (c) 2011-2016, Alexey Kramarenko
    All rights reserved.
*/

#include "vfs.h"
#include "process.h"
#include <string.h>
#include "stdlib.h"

extern void fat16();

HANDLE vfs_create(unsigned int process_size, unsigned int priority)
{
    REX rex;
    rex.name = "FAT16";
    rex.size = process_size;
    rex.priority = priority;
    rex.flags = PROCESS_FLAGS_ACTIVE;
    rex.fn = fat16;
    return process_create(&rex);

}

bool vfs_mount(HANDLE vfs, VFS_VOLUME_TYPE* volume)
{
    IO* io = io_create(sizeof(VFS_VOLUME_TYPE));
    if (io == NULL)
        return false;
    memcpy(io_data(io), volume, sizeof(VFS_VOLUME_TYPE));
    io->data_size = sizeof(VFS_VOLUME_TYPE);
    return io_write_sync(vfs, HAL_IO_REQ(HAL_VFS, VFS_MOUNT), 0, io) == sizeof(VFS_VOLUME_TYPE);
}

void vfs_unmount(HANDLE vfs)
{
    ack(vfs, HAL_REQ(HAL_VFS, VFS_UNMOUNT), 0, 0, 0);
}

bool vfs_record_create(HANDLE vfs, VFS_RECORD_TYPE *vfs_record)
{
    //VFS_FIND_TYPE is the biggest one
    vfs_record->io = io_create(sizeof(VFS_FIND_TYPE));
    if (vfs_record->io == NULL)
        return false;
    vfs_record->current_folder = VFS_ROOT;
    vfs_record->vfs = vfs;
    return true;
}

void vfs_record_destroy(VFS_RECORD_TYPE* vfs_record)
{
    io_destroy(vfs_record->io);
}

HANDLE vfs_find_first(VFS_RECORD_TYPE* vfs_record)
{
    return get_handle(vfs_record->vfs, HAL_REQ(HAL_VFS, VFS_FIND_FIRST), vfs_record->current_folder, 0, 0);
}

bool vfs_find_next(VFS_RECORD_TYPE* vfs_record, HANDLE find_handle)
{
    int res;
    res = io_read_sync(vfs_record->vfs, HAL_IO_REQ(HAL_VFS, VFS_FIND_NEXT), find_handle, vfs_record->io, sizeof(VFS_FIND_TYPE));
    if (res < 0)
        return false;
    return true;
}

void vfs_find_close(VFS_RECORD_TYPE* vfs_record, HANDLE find_handle)
{
    ack(vfs_record->vfs, HAL_REQ(HAL_VFS, VFS_FIND_CLOSE), find_handle, 0, 0);
}

void vfs_cd(VFS_RECORD_TYPE* vfs_record, unsigned int folder)
{
    vfs_record->current_folder = folder;
}

bool vfs_cd_up(VFS_RECORD_TYPE* vfs_record)
{
    unsigned int folder;
    error(ERROR_OK);
    folder = get(vfs_record->vfs, HAL_REQ(HAL_VFS, VFS_CD_UP), vfs_record->current_folder, 0, 0);
    if (get_last_error() == ERROR_OK)
    {
        vfs_record->current_folder = folder;
        return true;
    }
    return false;
}

bool vfs_cd_path(VFS_RECORD_TYPE* vfs_record, const char* path)
{
    int res;
    strcpy(io_data(vfs_record->io), path);
    vfs_record->io->data_size = strlen(path) + 1;
    res = io_write_sync(vfs_record->vfs, HAL_IO_REQ(HAL_VFS, VFS_CD_PATH), vfs_record->current_folder, vfs_record->io);
    if (res != sizeof(unsigned int))
        return false;
    vfs_record->current_folder = *((unsigned int*)io_data(vfs_record->io));
    return true;
}

char* vfs_read_volume_label(VFS_RECORD_TYPE* vfs_record)
{
    int res;
    res = io_read_sync(vfs_record->vfs, HAL_IO_REQ(HAL_VFS, VFS_READ_VOLUME_LABEL), 0, vfs_record->io, VFS_MAX_FILE_PATH + 1);
    if (res < 0)
        return NULL;
    return (char*)io_data(vfs_record->io);
}

HANDLE vfs_open(VFS_RECORD_TYPE* vfs_record, const char* file_path, unsigned int mode)
{
    int res;
    VFS_OPEN_TYPE* ot = io_data(vfs_record->io);
    ot->mode = mode;
    strcpy(ot->name, file_path);
    vfs_record->io->data_size = sizeof(VFS_OPEN_TYPE) - VFS_MAX_FILE_PATH + strlen(file_path);
    res = io_write_sync(vfs_record->vfs, HAL_IO_REQ(HAL_VFS, IPC_OPEN), vfs_record->current_folder, vfs_record->io);
    if (res != sizeof(HANDLE))
        return INVALID_HANDLE;
    return *((HANDLE*)io_data(vfs_record->io));
}

bool vfs_seek(VFS_RECORD_TYPE* vfs_record, HANDLE handle, unsigned int pos)
{
    return get_size(vfs_record->vfs, HAL_REQ(HAL_VFS, IPC_SEEK), handle, pos, 0) >= 0;
}

void vfs_read(VFS_RECORD_TYPE* vfs_record, HANDLE handle, IO* io, unsigned int size)
{
    io_read(vfs_record->vfs, HAL_IO_REQ(HAL_VFS, IPC_READ), handle, io, size);
}

int vfs_read_sync(VFS_RECORD_TYPE* vfs_record, HANDLE handle, IO* io, unsigned int size)
{
    return io_read_sync(vfs_record->vfs, HAL_IO_REQ(HAL_VFS, IPC_READ), handle, io, size);
}

void vfs_close(VFS_RECORD_TYPE* vfs_record, HANDLE handle)
{
    ack(vfs_record->vfs, HAL_REQ(HAL_VFS, IPC_CLOSE), handle, 0, 0);
}
