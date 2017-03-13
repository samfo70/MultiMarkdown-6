/**

	MultiMarkdown -- Lightweight markup processor to produce HTML, LaTeX, and more.

	@file epub.c

	@brief 


	@author	Fletcher T. Penney
	@bug	

**/

/*

	Copyright © 2016 - 2017 Fletcher T. Penney.


	The `MultiMarkdown 6` project is released under the MIT License..
	
	GLibFacade.c and GLibFacade.h are from the MultiMarkdown v4 project:
	
		https://github.com/fletcher/MultiMarkdown-4/
	
	MMD 4 is released under both the MIT License and GPL.
	
	
	CuTest is released under the zlib/libpng license. See CuTest.c for the
	text of the license.
	
	
	## The MIT License ##
	
	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files (the
	"Software"), to deal in the Software without restriction, including
	without limitation the rights to use, copy, modify, merge, publish,
	distribute, sublicense, and/or sell copies of the Software, and to
	permit persons to whom the Software is furnished to do so, subject to
	the following conditions:
	
	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.
	
	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
	CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
	TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
	

*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <uuid/uuid.h>

#include "d_string.h"
#include "epub.h"
#include "html.h"
#include "miniz.h"
#include "mmd.h"
#include "writer.h"


#define print(x) d_string_append(out, x)
#define print_const(x) d_string_append_c_array(out, x, sizeof(x) - 1)
#define print_char(x) d_string_append_c(out, x)
#define printf(...) d_string_append_printf(out, __VA_ARGS__)
#define print_token(t) d_string_append_c_array(out, &(source[t->start]), t->len)
#define print_localized(x) mmd_print_localized_char_html(out, x, scratch)


char * epub_mimetype(void) {
	return strdup("application/epub+zip");
}


char * epub_container_xml(void) {
	DString * container = d_string_new("");

	d_string_append(container, "<?xml version=\"1.0\"?>\n");
	d_string_append(container, "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n");
	d_string_append(container, "<rootfiles>\n");
	d_string_append(container, "<rootfile full-path=\"Content/main.opf\" media-type=\"application/oebps-package+xml\" />\n");
	d_string_append(container, "</rootfiles>\n");
	d_string_append(container, "</container>\n");

	char * result = container->str;
	d_string_free(container, false);
	return result;	
}


char * epub_package_document(scratch_pad * scratch) {
	DString * out = d_string_new("");

	meta * m;

	d_string_append(out, "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>\n");
	d_string_append(out, "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"pub-id\">\n");


	// Metadata
	d_string_append(out, "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n");	

	// Identifier
	HASH_FIND_STR(scratch->meta_hash, "uuid", m);
	if (m) {
		print_const("<dc:identifier id=\"pub-id\">urn:uuid:");
		mmd_print_string_html(out, m->value, false);
		print_const("</dc:identifier>\n");
	} else {
		print_const("<dc:identifier id=\"pub-id\">urn:uuid:");
		uuid_t u;
		uuid_generate_random(u);
		char id[36] = "";
		uuid_unparse_lower(u, id);
		print(id);
		print_const("</dc:identifier>\n");
	}

	// Title
	HASH_FIND_STR(scratch->meta_hash, "title", m);
	if (m) {
		print_const("<dc:title>");
		mmd_print_string_html(out, m->value, false);
		print_const("</dc:title>\n");
	} else {
		print_const("<dc:title>Untitled</dc:title>\n");
	}

	// Author
	HASH_FIND_STR(scratch->meta_hash, "author", m);
	if (m) {
		print_const("<dc:creator>");
		mmd_print_string_html(out, m->value, false);
		print_const("</dc:creator>\n");
	}


	// Language
	HASH_FIND_STR(scratch->meta_hash, "language", m);
	if (m) {
		print_const("<dc:language>");
		mmd_print_string_html(out, m->value, false);
		print_const("</dc:language>\n");
	} else {
		print_const("<dc:language>en</dc:language>\n");
	}

	// Date
	HASH_FIND_STR(scratch->meta_hash, "date", m);
	if (m) {
		print_const("<meta property=\"dcterms:modified\">");
		mmd_print_string_html(out, m->value, false);
		print_const("</meta>\n");
	} else {
		time_t t = time(NULL);
		struct tm * today = localtime(&t);

		d_string_append_printf(out, "<meta property=\"dcterms:modified\">%d-%02d-%02d</meta>\n",
			today->tm_year + 1900, today->tm_mon + 1, today->tm_mday);
	}

	d_string_append(out, "</metadata>\n");


	// Manifest
	d_string_append(out, "<manifest>\n");
    d_string_append(out, "<item id=\"nav\" href=\"nav.xhtml\" properties=\"nav\" media-type=\"application/xhtml+xml\"/>\n");
    d_string_append(out, "<item id=\"main\" href=\"main.xhtml\" media-type=\"application/xhtml+xml\"/>\n");
	d_string_append(out, "</manifest>\n");

	// Spine
	d_string_append(out, "<spine>\n");
    d_string_append(out, "<itemref idref=\"main\"/>");
	d_string_append(out, "</spine>\n");

	d_string_append(out, "</package>\n");

	char * result = out->str;
	d_string_free(out, false);
	return result;
}


void epub_export_nav_entry(DString * out, const char * source, scratch_pad * scratch, size_t * counter, short level) {
	token * entry, * next;
	short entry_level, next_level;
	char * temp_char;

	print_const("\n<ol>\n");

	// Iterate over tokens
	while (*counter < scratch->header_stack->size) {
		// Get token for header
		entry = stack_peek_index(scratch->header_stack, *counter);
		entry_level = raw_level_for_header(entry);

		if (entry_level >= level) {
			// This entry is a direct descendant of the parent
			temp_char = label_from_header(source, entry);
			printf("<li><a href=\"main.xhtml#%s\">", temp_char);
			mmd_export_token_tree_html(out, source, entry->child, scratch);
			print_const("</a>");

			if (*counter < scratch->header_stack->size - 1) {
				next = stack_peek_index(scratch->header_stack, *counter + 1);
				next_level = next->type - BLOCK_H1 + 1;
				if (next_level > entry_level) {
					// This entry has children
					(*counter)++;
					epub_export_nav_entry(out, source, scratch, counter, entry_level + 1);
				}
			}

			print_const("</li>\n");
			free(temp_char);
		} else if (entry_level < level ) {
			// If entry < level, exit this level
			// Decrement counter first, so that we can test it again later
			(*counter)--;
			break;
		}
		
		// Increment counter
		(*counter)++;
	}

	print_const("</ol>\n");
}


void epub_export_nav(DString * out, mmd_engine * e, scratch_pad * scratch) {
	size_t counter = 0;


	epub_export_nav_entry(out, e->dstr->str, scratch, &counter, 0);
}


char * epub_nav(mmd_engine * e, scratch_pad * scratch) {
	meta * temp;

	DString * out = d_string_new("");

	d_string_append(out, "<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">\n");

	d_string_append(out, "<head>\n<title>");
	HASH_FIND_STR(scratch->meta_hash, "title", temp);
	if (temp) {
		mmd_print_string_html(out, temp->value, false);
	} else {
		print_const("Untitled");
	}
	print_const("</title>\n</head>\n");
	
	print_const("<body>\n<nav epub:type=\"toc\">\n");
	print_const("<h2>Table of Contents</h2>\n");

	epub_export_nav(out, e, scratch);

	print_const("</nav>\n</body>\n</html>\n");

	char * result = out->str;
	d_string_free(out, false);
	return result;	
}


// Use the miniz library to create a zip archive for the EPUB document
void epub_write_wrapper(const char * filepath, const char * body, mmd_engine * e) {
	scratch_pad * scratch = scratch_pad_new(e, FORMAT_EPUB);
	mz_bool status;
	char * data;
	size_t len;

	// Delete existing file, if present
	remove(filepath);

	// Add mimetype
	data = epub_mimetype();
	len = strlen(data);
	status = mz_zip_add_mem_to_archive_file_in_place(filepath, "mimetype", data, len, NULL, 0, MZ_BEST_COMPRESSION);
	free(data);

	// Create directories
	status = mz_zip_add_mem_to_archive_file_in_place(filepath, "Content/", NULL, 0, NULL, 0, MZ_BEST_COMPRESSION);
	status = mz_zip_add_mem_to_archive_file_in_place(filepath, "META-INF/", NULL, 0, NULL, 0, MZ_BEST_COMPRESSION);

	// Add container
	data = epub_container_xml();
	len = strlen(data);
	status = mz_zip_add_mem_to_archive_file_in_place(filepath, "META-INF/container.xml", data, len, NULL, 0, MZ_BEST_COMPRESSION);
	free(data);

	// Add package
	data = epub_package_document(scratch);
	len = strlen(data);
	status = mz_zip_add_mem_to_archive_file_in_place(filepath, "Content/main.opf", data, len, NULL, 0, MZ_BEST_COMPRESSION);
	free(data);

	// Add nav
	data = epub_nav(e, scratch);
	len = strlen(data);
	status = mz_zip_add_mem_to_archive_file_in_place(filepath, "Content/nav.xhtml", data, len, NULL, 0, MZ_BEST_COMPRESSION);
	free(data);

	// Add document
	len = strlen(body);
	status = mz_zip_add_mem_to_archive_file_in_place(filepath, "Content/main.xhtml", body, len, NULL, 0, MZ_BEST_COMPRESSION);

	scratch_pad_free(scratch);
}



