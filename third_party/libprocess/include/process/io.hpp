#ifndef __PROCESS_IO_HPP__
#define __PROCESS_IO_HPP__

#include <process/future.hpp>

namespace process {
namespace io {

// Possible events for polling.
const short READ = 0x01;
const short WRITE = 0x02;

// Returns the events (a subset of the events specified) that can be
// performed on the specified file descriptor without blocking.
Future<short> poll(int fd, short events);

// TODO(benh): Add a version which takes multiple file descriptors.


// Set the open file descriptor fd to be non-blocking. If succeeds, the function
// will return 0. Otherwise, -1 will be returned and the errno will be set
// accordingly.
int nonblock(int fd);


// Check whether the open file descriptor fd is non-blocking. The function will
// return 1 if fd is non-blocking, 0 if fd is blocking, -1 if error occurs. The
// errno will be set accordingly if error is returned.
int isNonblock(int fd);


// Performs a single non-blocking read by polling on the specified file
// descriptor until any data can be be read. The future will become ready when
// some data is read (may be less than that specified by size). A future failure
// will be returned if an error is detected. If end-of-file is reached, value
// zero will be returned. Note that the return type of this function differs
// from the standard 'read'. In particular, this function returns the number of
// bytes read or zero on end-of-file (an error is indicated by failing the
// future, thus only a 'size_t' is necessary rather than a 'ssize_t').
Future<size_t> read(int fd, void* data, size_t size);

} // namespace io {
} // namespace process {

#endif // __PROCESS_IO_HPP__
