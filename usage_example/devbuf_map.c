#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Definitions for using IOCTL commands to get the buffer details
 */
#define U_DMA_BUF_IOCTL_MAGIC               'U'
#define U_DMA_BUF_IOCTL_GET_SIZE            _IOR (U_DMA_BUF_IOCTL_MAGIC, 2, uint64_t)
#define U_DMA_BUF_IOCTL_GET_DMA_ADDR        _IOR (U_DMA_BUF_IOCTL_MAGIC, 3, uint64_t)

/*
 * Structure to hold the device buffer details.
 * Only the start physical address and the size of the buffer is used for now.
 */

typedef struct devbuf_details_t{
	uint64_t  start_phys_addr;
	size_t    size;
}devbuf_details;

/*
 * Open a file and check for errors.
 * 
 * @param pathname: Path to the file to open
 * @param flags: Flags to open the file with
 * 
 * @return: File descriptor on success 
 */
static int checked_open(const char *pathname, int flags)
{
	int fd = open(pathname, flags);

	if (fd < 0) {
		perror(pathname);
		exit(EXIT_FAILURE);
	}

	return fd;
}

/*  
 * Get the buffer details for a device buffer.
 * Uses IOCTL commands to get the buffer size and the physical address of the buffer.
 * 
 * @param fd: File descriptor of the device buffer
 * @param devbuf: Pointer to the devbuf_details structure to store the buffer details
 * 
 * @return: 0 on success, -1 on failure
 */

static int get_device_buffer_details_filedes(int fd, devbuf_details *devbuf)
{
    int ret;
    uint64_t size;
    uint64_t phys_addr;

    ret = ioctl(fd, U_DMA_BUF_IOCTL_GET_SIZE, &size);
    if (ret < 0) {
        perror("ioctl U_DMA_BUF_IOCTL_GET_SIZE");
        return -1;
    }
    printf("buf_size: %lu\n", size);

    ret = ioctl(fd, U_DMA_BUF_IOCTL_GET_DMA_ADDR, &phys_addr);
    if (ret < 0) {
        perror("ioctl U_DMA_BUF_IOCTL_GET_DMA_ADDR");
        return -1;
    }
    printf("phys_addr: %lx\n", phys_addr);

    if (devbuf)
    {
        devbuf->size = size;
		devbuf->start_phys_addr = phys_addr;
    }
    return 0;
}

/*
* Get the buffer details for a device buffer.
* 
* @param buf_num: Number of the device buffer
* @param devbuf: Pointer to the devbuf_details structure to store the buffer details
* 
* @return: 0 on success, -1 on failure    

*/
static int get_device_buffer_details(int buf_num, devbuf_details *devbuf)
{
    char dev_path[BUFSIZ];
    int ret;
    uint64_t size;
    uint64_t phys_addr;
    int fd;

    snprintf(dev_path, sizeof(dev_path), "/dev/udmabuf%d", buf_num);
    fd = checked_open(dev_path, O_RDWR);

	if (fd != -1) {
        ret = get_device_buffer_details_filedes(fd, devbuf);
        close(fd);
    }

    return 0;
}

/*
* Convert a virtual address to a physical address.
*
* IN: buf_num, virt_addr, map_start_virt_addr, dev_map_offset
* @param buf_num: Number of the device buffer
* @param virt_addr: Virtual address to convert
* @param map_start_virt_addr: Starting virtual address of the mapped buffer
* @param dev_map_offset: Offset of the mapped buffer in the device
*
* OUT: phys_addr
* @param phys_addr: Pointer to store the physical address
* 
* @return: 0 on success, -1 on failure
*/
static int devbuf_virt_to_phys(int       buf_num, 
                               void     *virt_addr, 
                               void     *map_start_virt_addr,
                               uint64_t  dev_map_offset,
                               uint64_t *phys_addr)
{
    char dev_path[BUFSIZ];
    int ret;
    int fd;
    uint64_t offset;
    devbuf_details devbuf;

    printf("devbuf_virt_to_phys: buf_num: %d, virt_addr: %p, map_start_virt_addr: %p, dev_map_offset: %lx\n", 
            buf_num, virt_addr, map_start_virt_addr, dev_map_offset);
    if (!phys_addr){
        printf("Invalid phys_addr pointer\n");
        return -1;
    }

    if (virt_addr < map_start_virt_addr){
        printf("Invalid virt_addr, less than starting dev map address\n");
        return -1;
    }
    offset = (uint64_t)virt_addr - (uint64_t)map_start_virt_addr + dev_map_offset;

    ret = get_device_buffer_details(buf_num, &devbuf);
    if (ret < 0) 
        return -1;

    if (offset > devbuf.size){
        printf("Invalid offset, greater than buffer size\n");
        return -1;
    }

    *phys_addr = devbuf.start_phys_addr + offset;
    return 0;
}


/*
* Map a device buffer to user space.
*
* IN: buf_num, size   
* @param buf_num: Number of the device buffer
* @param size: Size of the buffer to map
*
* OUT: map_addr, devbuf
* @param map_addr: Pointer to store the mapped address
* @param devbuf: Pointer to the devbuf_details structure to store the buffer details
*   
*/
int map_device_buffer(unsigned int buf_num, size_t size, void **map_addr, devbuf_details *devbuf)
{
    char dev_path[BUFSIZ];
	void *addr;
    int fd;

    snprintf(dev_path, sizeof(dev_path), "/dev/udmabuf%d", buf_num);
    fd = checked_open(dev_path, O_RDWR);

	if (fd != -1) {
        printf("Getting device buffer details for %s\n", dev_path);
        get_device_buffer_details_filedes(fd, devbuf);

        printf("mapping device file %s\n", dev_path);
        addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		*map_addr = addr;
		close(fd);
    }

    return 0;
}

int main(int argc, char **argv)
{
    char buffer[BUFSIZ];
    int maps_fd;
	void *map_addr;
	size_t buf_size;
    devbuf_details devbuf;
    uint64_t phys_addr;


    if (argc < 2) {
        printf("Usage: %s size (in MB)\n", argv[0]);
        return EXIT_FAILURE;
    }
    buf_size = strtoull(argv[1], NULL, 0);
	buf_size = buf_size << 20;

	map_device_buffer(0, buf_size, &map_addr, &devbuf);
	printf("map_addr: %p\n", map_addr);

    devbuf_virt_to_phys(0, (map_addr + 4096), map_addr, 0, &phys_addr);
    printf("phys_addr: %lx\n", phys_addr);

    return EXIT_SUCCESS;
}

