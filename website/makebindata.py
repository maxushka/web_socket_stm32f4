#-*- coding: utf-8 -*-
import os
from os.path import isfile, join
import codecs
import io
import ctypes


try:
	from StringIO import StringIO  # Python 2.7

except ImportError:
	from io import StringIO  # Python 3.x
import gzip

def convert_struct_to_bytes(st):
	buffer = ctypes.create_string_buffer(ctypes.sizeof(st))
	ctypes.memmove(buffer, ctypes.addressof(st), ctypes.sizeof(st))
	return buffer.raw

class Files(ctypes.Structure):
	_pack_ = 1
	_fields_ = [
	('file_name', ctypes.c_char*64),
	('offset', ctypes.c_uint32),
	('page_size', ctypes.c_uint32) ]
	def __init__(self, file_name, offset, page_size):
		self.file_name = file_name
		self.offset = offset
		self.page_size = page_size

class NumFiles(ctypes.Structure):
	_pack_ = 1
	_fields_ = [
	('file_cnt', ctypes.c_uint32) ]
	def __init__(self, file_cnt):
		self.file_cnt = file_cnt

bin_file = open('fsdata.bin', 'wb')
path="fs/"
from os import walk
files = [os.path.join(r,file) for r,d,f in os.walk(path) for file in f]
#print(files)
nmf = NumFiles(len(files))
bf = convert_struct_to_bytes(nmf)
bin_file.write(bf)

total_len = 0
start_size = ctypes.sizeof(nmf)+ctypes.sizeof(Files('',0,0))*nmf.file_cnt


def bin_to_gz(bin, mode='wb', encoding=None):
    out = StringIO()
    with gzip.GzipFile(fileobj=out, mode=mode) as f:
        f.write(bin)
    v = out.getvalue()
    return v
    

for file in files:
	tmp = open(file, 'r+b')
	file = file.replace("fs/", "", 1)
	file = file.replace("\\", "/")
	fstr = Files(file, start_size+total_len, 0)

	if file.find('.html') != -1:
		header = b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: text/html\nConnection: close\r\n\r\n"
	if file.find('.js') != -1:
		header = b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: application/javascript\nConnection: close\r\n\r\n"
	if file.find('.css') != -1:
		header = b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: text/css\nConnection: close\r\n\r\n"
	if file.find('.jpg') != -1:
		header = b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: image/jpeg\nConnection: close\r\n\r\n"
	if file.find('.png') != -1:
		header = b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: image/png\nConnection: close\r\n\r\n"
	if file.find('.svg') != -1:
		header = b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: image/svg+xml\nConnection: close\r\n\r\n"
	if file.find('.ico') != -1:
		header = b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: image/x-icon\nConnection: close\r\n\r\n"

	
	fstr.page_size = len(bin_to_gz(tmp.read()))+len(header)
	total_len += fstr.page_size
	buff = convert_struct_to_bytes(fstr)
	bin_file.write(buff)
	tmp.close()

for file in files:
	bin_file.write(b"HTTP/1.1 200 OK\nContent-Encoding: gzip\nContent-Type: ")
	if file.find('.html') != -1:
		bin_file.write(b"text/html\nConnection: close\r\n\r\n")
	if file.find('.js') != -1:
		bin_file.write(b"application/javascript\nConnection: close\r\n\r\n")
	if file.find('.css') != -1:
		bin_file.write(b"text/css\nConnection: close\r\n\r\n")
	if file.find('.jpg') != -1:
		bin_file.write(b"image/jpeg\nConnection: close\r\n\r\n")
	if file.find('.png') != -1:
		bin_file.write(b"image/png\nConnection: close\r\n\r\n")
	if file.find('.svg') != -1:
		bin_file.write(b"image/svg+xml\nConnection: close\r\n\r\n")
	if file.find('.ico') != -1:
		bin_file.write(b"image/x-icon\nConnection: close\r\n\r\n")
	print(file)
	tmp = open(file, 'r+b')
	bin_file.write(bin_to_gz(tmp.read()))
	#bin_file.write(b"\0")

bin_file.close()


# class Num(ctypes.Structure):
# 	_pack_ = 1
# 	_fields_ = [
# 	('file_cnt', ctypes.c_uint8) ]
# 	def __init__(self, file_cnt):
# 		self.file_cnt = file_cnt

# arr_file = open('fsdata.bin', 'wb')
# for n in range(0,257):
# 	for i in range(1,256):
# 		bbb = Num(i)
# 		buff = convert_struct_to_bytes(bbb)
# 		arr_file.write(buff)
# arr_file.close()
#ctypes.sizeof(nmf)+ctypes.sizeof(Files())
#f.write(b"Hello, i'am bin file")

# import os
# from os.path import isfile, join
# import codecs
# import io

# def fileNames(path_name, names):
# 	files=[]
# 	cataloges=[]
# 	for name in names:
# 		if isfile(join(path_name, name)):
# 			files.append(name)
# 		else:
# 			cataloges.append(name)
# 	files_and_cataloges=[files, cataloges]
# 	return files_and_cataloges

# def printFiles(list_write_files, path_name, write):
	
# 	for name in list_write_files:
# 		read_file=open(path_name+"\\"+name)
# 		read=read_file.read()
# 		write.write('\n' + name + '\n')
# 		write.write(read)


# def rec(path, catalog, write_file):
# 	for name in catalog:
# 		listpath=os.listdir(path+"\\"+name)
# 		files_and_cataloges=fileNames(path+"\\"+name, listpath)
# 		print(name)
# 		printFiles(files_and_cataloges[0], path+"\\"+name, write_file)
# 		rec(path+"\\"+name, files_and_cataloges[1], write_file)


# if __name__ == "__main__":

# 	write_file=open("fsdata.bin", "wb")

# 	path="fs"
# 	rec(path, [""], write_file)
