#!/usr/bin/env python3

from argparse import ArgumentParser, RawTextHelpFormatter
from enum import IntEnum
from typing import List
from collections import defaultdict

from astropy import units as u
from astropy.coordinates import SkyCoord
from astroquery.simbad import Simbad

import random
import operator
import math
import csv
import re
import struct
import time

INDEX_ENTRY_SIZE = 12

MAGIC_ID = 0x11223344
VERSION = 0

RA_SCALE = 1000000
DEC_SCALE = 100000
MAG_SCALE = 100

def to_sector(pos):
	return math.floor(pos/512)

def to_pos(sector):
	return sector * 512

def next_sector_pos(pos):
	return pos + (512 - (pos % 512))


class SkyObjectType(IntEnum):
	EMPTY = 0
	UNSPECIFIED = 1
	STAR = 2
	GALAXY = 3
	CLUSTER_GLOBULAR = 4
	CLUSTER_OPEN = 5
	NEBULA = 6
	INTERACTING_GALAXY = 7
	CLUSTER = 8

def simbad_type(sim_type: str):
	sky_obj_type = SkyObjectType.UNSPECIFIED

	if sim_type == "GlC":
		sky_obj_type = SkyObjectType.CLUSTER_GLOBULAR
	elif sim_type == "G" or sim_type == "LIN":
		sky_obj_type = SkyObjectType.GALAXY
	elif sim_type == "OpC":
		sky_obj_type = SkyObjectType.CLUSTER_OPEN
	elif sim_type == "IG":
		sky_obj_type = SkyObjectType.INTERACTING_GALAXY
	elif sim_type == "Cl*":
		sky_obj_type = SkyObjectType.CLUSTER
		

	return sky_obj_type


class ImageHeader:
	_PACK_STRING = "<IIII"
	SIZE = struct.calcsize(_PACK_STRING)
	magic_id: int
	version: int
	type_begin: int
	num_types: int

	def __init__(self, version, type_begin, num_types):
		self.magic_id = MAGIC_ID
		self.version = version
		self.type_begin = type_begin
		self.num_types = num_types

	def pack(self):
		return struct.pack(self._PACK_STRING, self.magic_id, self.version, self.type_begin, self.num_types)

class ImageTypeHeader:
	_PACK_STRING = "<BIII3s"
	SIZE = struct.calcsize(_PACK_STRING)
	sky_type: SkyObjectType
	data_begin: int
	name_begin: int
	num_entries: int
	padding: int

	def __init__(self, sky_type, data_begin, name_begin, num_entries):
		self.sky_type = sky_type
		self.data_begin = data_begin
		self.name_begin = name_begin
		self.num_entries = num_entries
		self.padding = b""
		print("Created ImageTypeHeader with data {} name {} num {}".format(data_begin, name_begin, num_entries))

	def pack(self):
		return struct.pack(self._PACK_STRING, self.sky_type, self.data_begin, self.name_begin, self.num_entries, self.padding)

class ImageEntry:
	_PACK_STRING = "<IiihH"
	SIZE = struct.calcsize(_PACK_STRING)
	ID: int
	RA: int
	dec: int
	mag: int
	flags: int

	def __init__(self, RA, dec, mag, flags, ID = None):
		if ID is None:
			ID = random.randint(0, 2**31)
		self.ID = ID
		self.RA = RA
		self.dec = dec
		self.mag = mag
		self.flags = flags

	def __str__(self):
		return "{}".format(self.mag)

	def pack(self):
		return struct.pack(self._PACK_STRING, self.ID, self.RA, self.dec, self.mag, self.flags)

class ImageNameEntry:
	_PACK_STRING = "8s24s"
	SIZE = struct.calcsize(_PACK_STRING)
	shortname: str
	longname: str

	def __init__(self, shortname, longname):
		self.shortname = shortname
		self.longname = longname

	def pack(self):
		return struct.pack(self._PACK_STRING, self.shortname, self.longname)

assert ImageTypeHeader.SIZE == 16
assert ImageHeader.SIZE == 16
assert ImageEntry.SIZE == 16
assert ImageNameEntry.SIZE == 32
assert ImageNameEntry.SIZE % ImageEntry.SIZE == 0


class SkyObject:
	objectType: SkyObjectType
	RA: int
	DEC: int
	identifier: str
	name: str
	mag: int

	def __init__(self, RA, DEC, identifier, name, mag, objectType = 0 ):
		self.objectType = objectType
		self.RA = RA
		self.DEC = DEC
		self.identifier = identifier
		self.name = name
		self.mag = mag

	def __str__(self):
		return "Skyobject with type {}. RA: {}, DEC: {}, id: {}, name: {}, mag: {}".format(self.objectType, self.RA, self.DEC, self.identifier, self.name, self.mag)


def kstars_binary_read_header(catalog):
	header_text = catalog.read(124)
	print("Header is: {}".format(header_text))
	endianess = catalog.read(2)
	if(endianess != b'SK'):
		print("Byteswapping not implemented!")
		return
	version = struct.unpack('b', catalog.read(1))[0]
	print("Version: {}".format(version))
	num_fields = struct.unpack('h', catalog.read(2))[0]
	print("Number of fields: {}".format(num_fields))
	for i in range(0, num_fields):
		field = struct.unpack('10sbBi', catalog.read(16))
		print("Field: {}".format(field))
	trixels = struct.unpack('i', catalog.read(4))[0]
	print("Number of trixels: {}".format(trixels))
	print("header done! file pos: {}".format(catalog.tell()))
	return (catalog.tell(), trixels)


def parse_kstars_binary(filename, starname_file = "/usr/share/kstars/starnames.dat") -> List[SkyObject]:
	sky_objects = []
	with open(filename, "rb") as catalog, open(starname_file, "rb") as starnames:
		header_size, trixels = kstars_binary_read_header(catalog)
		header_size_name = kstars_binary_read_header(starnames)[0]
		# header done
		starnames.seek(header_size_name + 1 * INDEX_ENTRY_SIZE)
		for t in range(0, trixels):
			catalog.seek(header_size + t * INDEX_ENTRY_SIZE) # seek to a specific trixel
			f_trix = struct.unpack('i', catalog.read(4))[0]
			print("Read trixel: {}".format(f_trix))
			offset = struct.unpack('i', catalog.read(4))[0]
			nrec = struct.unpack('i', catalog.read(4))[0]
			print("offset: {}, nrec: {}".format(offset, nrec))
			catalog.seek(offset)
			for i in range(0, nrec):
				offset = catalog.tell()
				star_data = struct.unpack('iiiiiihhhbb', catalog.read(32))
				print("Star data: {}".format(star_data))
				shortname = b""
				longname = b""
				if star_data[9] & 0x01:
					print("has name!")
					shortname = struct.unpack('8s', starnames.read(8))[0]
					longname = struct.unpack('32s', starnames.read(32))[0]
					print("Shortname: {} Longname: {}".format(shortname, longname))
				sky_objects.append(SkyObject(RA = star_data[0], DEC = star_data[1], mag = star_data[6], identifier = shortname, name = longname))
	return sky_objects


def parse_kstars_catalog(filename):
	with open(filename) as catalog:
		# find field name
		pattern = re.compile("^#.* RA .*DEC.*")
		for row in catalog:
			match = pattern.match(row)
			if match is not None:
				fields = match.group().split(" ")
				fields = list(filter(None, fields))[1:]
				break

		csv_reader = csv.DictReader(filter(lambda row: row[0]!='#', fp), delimiter=' ', fieldnames=fields)
		for row in csv_reader:
			print("Row: {}".format(row))

def write_image(outputfilename, objects: List[SkyObject]):
	objects.sort(key=operator.attrgetter('mag'))
	with open(outputfilename, "wb") as outfile:
		# gather types
		types = defaultdict(lambda: [])
		names = []
		for o in objects:
			entry = ImageEntry(RA=o.RA, dec = o.DEC, mag=o.mag, flags= 1)
			name_entry = ImageNameEntry(o.identifier, o.name)
			types[o.objectType].append([entry, name_entry])
			names.append(name_entry)
		# they should already be sorted by mag
		image_header = ImageHeader(0, 1, len(types.keys()))
		outfile.write(image_header.pack())

		# total size for all type entries
		type_entry_size = len(types.keys()) * ImageTypeHeader.SIZE
		type_entry_sectors = math.ceil(type_entry_size / 512)
		data_entry_size = len(objects) * ImageEntry.SIZE
		data_entry_sectors = math.ceil(data_entry_size / 512)

		print("type begin sector: {} to pos {}".format(image_header.type_begin, to_pos(image_header.type_begin)))
		type_entry_begin = to_pos(image_header.type_begin)
		data_entry_begin = next_sector_pos(type_entry_begin + type_entry_size)
		name_entry_begin = next_sector_pos(data_entry_begin + data_entry_size)

		print("Type entry begins at {} data {} name {}".format(type_entry_begin, data_entry_begin, name_entry_begin))

		# write type list
		outfile.seek(type_entry_begin)
		next_data_entry_begin = data_entry_begin
		next_name_entry_begin = name_entry_begin
		type_pos = type_entry_begin
		for t, e in types.items():
			outfile.seek(type_pos)
			type_list = ImageTypeHeader(t, to_sector(next_data_entry_begin), to_sector(next_name_entry_begin), len(e))
			print("Writing type header at pos: {}".format(outfile.tell()))
			outfile.write(type_list.pack())
			type_pos = outfile.tell()
			outfile.seek(next_data_entry_begin)
			for entry, name in e:
				outfile.write(entry.pack())
			outfile.seek(next_sector_pos(outfile.tell())) # seek to begin of next sector
			next_data_entry_begin = outfile.tell()
			outfile.seek(next_name_entry_begin)
			for entry, name in e:
				outfile.write(name.pack())
			outfile.seek(next_sector_pos(outfile.tell())) # seek to begin of next sector
			next_name_entry_begin = outfile.tell()


		print("type entry size {} takes {} sectors. data entry size {} takes {} sectors".format(type_entry_size, type_entry_sectors, data_entry_size, data_entry_sectors))

def get_simbad(catalog = None, max_mag = None, query_all=True):
	sky_objects = []
	Simbad.add_votable_fields('flux(V)')
	Simbad.add_votable_fields('otype(3)')
	Simbad.add_votable_fields('ids')
	Simbad.TIMEOUT = 30000
	print("Downloading data for {}...".format(catalog))
	criteria = []
	messier_data = []
	if catalog is not None:
		criteria.append("cat='{}'".format(catalog))
	if max_mag is not None:
		criteria.append("Vmag < {}".format(max_mag))
	if query_all:
		start_mag = -2
		end_mag = 11
		last_mag = start_mag
		current_mag_step = 1
		max_mag_step = 1
		while last_mag < end_mag:
			local_criteria = criteria.copy()
			local_criteria.append("Vmag > {} & Vmag <= {}".format(last_mag, last_mag + current_mag_step))
			try:
				local_data = Simbad.query_criteria("&".join(local_criteria))
				last_mag += current_mag_step
			except:
				print("Failed to get data. Reducing rate")
				current_mag_step /= 2
				local_data = None
			time.sleep(0.5)
			if local_data is not None:
				if len(local_data) >= 20000:
					# no clue if we can set type count using this lib?
					print("Got too much data, need to request again")
					current_mag_step /=4
					local_data = None
					continue
				messier_data += local_data
				if len(local_data) < 500:
					current_mag_step = min(max_mag_step, current_mag_step * 2)
				elif len(local_data) > 5000:
					current_mag_step /= 2
				print("Got {} entries in partial request! next step {} at mag {}".format(len(local_data), current_mag_step, last_mag))
	else:
		messier_data = Simbad.query_criteria("&".join(criteria))
		#messier_data = Simbad.query_object("m [1-9]", wildcard=True)
		#print(str(messier_data))
	print("Got {} entries!".format(len(messier_data)))
	for result in messier_data:
		try: # skip any missing data
			coord = SkyCoord(result["RA"], result["DEC"], unit=(u.deg, u.deg))
			ra = math.floor(coord.ra.degree*RA_SCALE + 0.5)
			dec = math.floor(coord.dec.degree*DEC_SCALE + 0.5)
			mag = math.floor(result["FLUX_V"]*MAG_SCALE + 0.5)
			# get a proper name
			name_table = result["IDS"]
			name = ""
			for row in name_table:
				row_str = str(row)
				if "NAME" in row_str:
					name = row_str.split('\n')[-1].split(' ', 1)[-1]
					break;
			obj = SkyObject(RA=ra, DEC=dec,identifier=result["MAIN_ID"], name=name.encode("utf-8"), mag=mag, objectType=simbad_type(result["OTYPE_3"].decode("utf-8")))
			sky_objects.append(obj)
		except ValueError as e:
			pass

	print("Processed data!")
	return sky_objects


if __name__ == "__main__":
	parser = ArgumentParser(formatter_class=RawTextHelpFormatter)
	parser.add_argument("-i", "--input", dest="input", action="append", help="Path to input file", type=str)
	parser.add_argument("-c", "--catalog", dest="catalogs", action="append", help="Catalog to download", type=str)
	args = parser.parse_args()
	sky_data = []
	if(args.catalogs is not None):
		for catalog in args.catalogs:
			sky_data += get_simbad(catalog)
	else:
		sky_data += get_simbad(query_all=True)
	if args.input is not None:
		for data_file in args.input:
			sky_data += parse_kstars_binary(data_file)
	# TODO: remove duplicates
	write_image("/tmp/test", sky_data)
