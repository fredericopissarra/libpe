/* vim: set ts=4 sw=4 noet: */
/*
	libpe - the PE library

	Copyright (C) 2010 - 2017 libpe authors

	This file is part of libpe.

	libpe is free software: you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	libpe is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with libpe.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "libpe/resources.h"
#include "libpe/dir_resources.h"
#include "libpe/pe.h"
#include "libpe/utlist.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// REFERENCE: https://msdn.microsoft.com/en-us/library/ms648009(v=vs.85).aspx
static const pe_resource_entry_info_t g_resource_dataentry_info_table[] = {
	{ "???_0",				0,					".0",		"_0"			},
	{ "RT_CURSOR",			RT_CURSOR,			".cur",		"cursors"		},
	{ "RT_BITMAP",			RT_BITMAP,			".bmp",		"bitmaps"		},
	{ "RT_ICON",			RT_ICON,			".ico",		"icons"			},
	{ "RT_MENU",			RT_MENU,			".rc",		"menus"			},
	{ "RT_DIALOG",			RT_DIALOG,			".dlg",		"dialogs"		},
	{ "RT_STRING",			RT_STRING,			".rc",		"strings"		},
	{ "RT_FONTDIR",			RT_FONTDIR,			".fnt",		"fontdirs"		},
	{ "RT_FONT",			RT_FONT,			".fnt",		"fonts"			},
	{ "RT_ACCELERATOR",		RT_ACCELERATOR,		".rc",		"accelerators"	},
	{ "RT_RCDATA",			RT_RCDATA,			".rc",		"rcdatas"		},
	{ "RT_MESSAGETABLE",	RT_MESSAGETABLE,	".mc",		"messagetables"	},
	{ "RT_GROUP_CURSOR",	RT_GROUP_CURSOR,	".cur",		"groupcursors"	},
	{ "???_13",				13,					".13",		"_13"			},
	{ "RT_GROUP_ICON",		RT_GROUP_ICON,		".ico",		"groupicons"	},
	{ "???_15",				15,					".15",		"_15"			},
	{ "RT_VERSION",			RT_VERSION,			".rc",		"versions"		},
	{ "RT_DLGINCLUDE",		RT_DLGINCLUDE,		".rc",		"dlgincludes"	},
	{ "???_18",				18,					".18",		"_18"			},
	{ "RT_PLUGPLAY",		RT_PLUGPLAY,		".rc",		"plugplays"		},
	{ "RT_VXD",				RT_VXD,				".rc",		"vxds"			},
	{ "RT_ANICURSOR",		RT_ANICURSOR,		".rc",		"anicursors"	},
	{ "RT_ANIICON",			RT_ANIICON,			".rc",		"aniicons"		},
	{ "RT_HTML",			RT_HTML,			".html",	"htmls"			},
	{ "RT_MANIFEST",		RT_MANIFEST,		".xml",		"manifests"		},
	{ "RT_DLGINIT",			RT_DLGINIT,			".rc",		"dlginits"		},
	{ "RT_TOOLBAR",			RT_TOOLBAR,			".rc",		"toolbars"		},
	{ NULL }
};

const pe_resource_entry_info_t *pe_resource_entry_info_lookup(uint32_t name_offset) {
	const pe_resource_entry_info_t *p;

	p = g_resource_dataentry_info_table;
	while ( p->name )
	{
		if ( p->type == name_offset )
			return p;
		p++;
	}

	return NULL;
}

void pe_resources_dealloc_node_search_result(pe_resource_node_search_result_t *result) {
	if (result == NULL)
		return;

	pe_resource_node_search_result_item_t *item = result->items;
	while (item != NULL) {
		pe_resource_node_search_result_item_t *next = item->next;
		free(item);
		item = next;
	}
}

void pe_resource_search_nodes(pe_resource_node_search_result_t *result, const pe_resource_node_t *node, pe_resource_node_predicate_fn predicate) {
	assert(result != NULL);

	if (node == NULL)
		return;

	if (predicate(node)) {
		// Found the matching node. Return it.
		pe_resource_node_search_result_item_t *item = calloc(1, sizeof(*item));
		if (item == NULL) {
			// TODO: Handle allocation failure.
			abort();
		}
		item->node = node;
		LL_APPEND(result->items, item);
		result->count++;
		// IMPORTANT: We do NOT return early because we want all matching nodes.
	}

	// Traverse the tree to find the matching node.
	pe_resource_search_nodes(result, node->childNode, predicate);
	pe_resource_search_nodes(result, node->nextNode, predicate);
}

pe_resource_node_t *pe_resource_root_node(const pe_resource_node_t *node) {
	if (node == NULL)
		return NULL;

	// Traverse the linked-list to find the root parent node.
	pe_resource_node_t *parent = node->parentNode;
	while (parent != NULL) {
		if (parent->parentNode == NULL) {
			// Found the root parent node. Return it.
			return parent;
		}
		// Move to the next parent node.
		parent = parent->parentNode;
	}

	return (pe_resource_node_t *)node; // Return the node itself if it has no parent.
}

pe_resource_node_t *pe_resource_last_child_node(const pe_resource_node_t *parent_node) {
	if (parent_node == NULL)
		return NULL;

	// Traverse the linked-list to find the last child node.
	pe_resource_node_t *child = parent_node->childNode;
	while (child != NULL) {
		if (child->nextNode == NULL) {
			// Found the last child node. Return it.
			return child;
		}
		// Move to the next node.
		child = child->nextNode;
	}

	return NULL;
}

pe_resource_node_t *pe_resource_find_node_by_type_and_level(const pe_resource_node_t *node, pe_resource_node_type_e type, uint32_t dirLevel) {
	if (node == NULL)
		return NULL;

	// Found the matching node. Return it.
	if (node->type == type && node->dirLevel == dirLevel) {
		return (pe_resource_node_t *)node;
	}

	// Traverse the tree to find the matching node.

	const pe_resource_node_t *child = pe_resource_find_node_by_type_and_level(node->childNode, type, dirLevel);
	// Found the matching node. Return it.
	if (child != NULL)
		return (pe_resource_node_t *)child;

	const pe_resource_node_t *sibling = pe_resource_find_node_by_type_and_level(node->nextNode, type, dirLevel);
	// Found the matching node. Return it.
	if (sibling != NULL)
		return (pe_resource_node_t *)sibling;

	return NULL;
}

pe_resource_node_t *pe_resource_find_parent_node_by_type_and_level(const pe_resource_node_t *node, pe_resource_node_type_e type, uint32_t dirLevel) {
	if (node == NULL)
		return NULL;

	// Traverse the linked-list to find the matching parent node.
	pe_resource_node_t *parent = node->parentNode;
	while (parent != NULL) {
		if (parent->type == type && parent->dirLevel == dirLevel) {
			// Found the matching parent node. Return it.
			return parent;
		}
		// Move to the next parent node.
		parent = parent->parentNode;
	}

	return NULL;
}

char *pe_resource_parse_string_u(pe_ctx_t *ctx, char *output, size_t output_size, const IMAGE_RESOURCE_DATA_STRING_U *data_string_ptr) {
	if (data_string_ptr == NULL)
		return NULL;

	if (!pe_can_read(ctx, data_string_ptr->String, data_string_ptr->Length)) {
		LIBPE_WARNING("Cannot read string from IMAGE_RESOURCE_DATA_STRING_U");
		return NULL;
	}

	// If the caller provided a NULL pointer, we do the allocation and return it.
	const size_t buffer_size = output_size == 0 ? (size_t)data_string_ptr->Length + 1 : output_size;
	if (output == NULL) {
		output = malloc(buffer_size);
		if (output == NULL) {
			// TODO: Handle allocation failure.
			abort();
		}
	}

	pe_utils_str_widechar2ascii(output, buffer_size, (const char *)data_string_ptr->String, (size_t)data_string_ptr->Length);

	return output;
}

static pe_resource_node_t *pe_resource_create_node(uint8_t depth, pe_resource_node_type_e type, void *raw_ptr, pe_resource_node_t *parent_node) {
	pe_resource_node_t *node = calloc(1, sizeof(pe_resource_node_t));
	if (node == NULL) {
		// TODO: Handle allocation failure.
		abort();
	}
	node->depth = depth;
	node->type = type;

	// Determine directory level.
	if (parent_node != NULL) {
		// node->dirLevel = parent_node->type == LIBPE_RDT_RESOURCE_DIRECTORY && node->type == LIBPE_RDT_DIRECTORY_ENTRY
		node->dirLevel = parent_node->type == LIBPE_RDT_RESOURCE_DIRECTORY
			? parent_node->dirLevel + 1
			: parent_node->dirLevel;
	} else {
		node->dirLevel = 0; // Only the root directory has dirLevel == 0.
	}

	// Establish relationships. Makes the node more human!
	if (parent_node != NULL) {
		node->parentNode = parent_node;

		if (parent_node->childNode == NULL) {
			// This is the 1st child node of parent_node.
			parent_node->childNode = node;
		} else {
			// This is NOT the 1st child node of parent_node, so we need to append it to the end of the linked-list.
			pe_resource_node_t *last_child_node = pe_resource_last_child_node(parent_node);
			if (last_child_node != NULL) {
				// Found the last child node. Append our new node.
				last_child_node->nextNode = node;
			}
		}
	}

	node->raw.raw_ptr = raw_ptr;

	switch (type) {
		default:
			LIBPE_WARNING("Invalid node type");
			break;
		case LIBPE_RDT_RESOURCE_DIRECTORY:
			node->raw.resourceDirectory = raw_ptr;
			break;
		case LIBPE_RDT_DIRECTORY_ENTRY:
			node->raw.directoryEntry = raw_ptr;
			break;
		case LIBPE_RDT_DATA_STRING:
			node->raw.dataString = raw_ptr;
			break;
		case LIBPE_RDT_DATA_ENTRY:
			node->raw.dataEntry = raw_ptr;
			break;
	}

	return node;
}

static void pe_resource_free_nodes(pe_resource_node_t *node) {
	if (node == NULL)
		return;

	pe_resource_free_nodes(node->childNode);
	pe_resource_free_nodes(node->nextNode);

	free(node->name);
	free(node);
}

static bool pe_resource_parse_nodes(pe_ctx_t *ctx, pe_resource_node_t *node) {
	switch (node->type) {
		default:
			LIBPE_WARNING("Invalid node type");
			return false;
		case LIBPE_RDT_RESOURCE_DIRECTORY:
		{
			const IMAGE_RESOURCE_DIRECTORY * const resdir_ptr = node->raw.resourceDirectory;
			IMAGE_RESOURCE_DIRECTORY_ENTRY *first_entry_ptr = LIBPE_PTR_ADD(resdir_ptr, sizeof(IMAGE_RESOURCE_DIRECTORY));
			const size_t total_entries = resdir_ptr->NumberOfIdEntries + resdir_ptr->NumberOfNamedEntries;

			for (size_t i = 0; i < total_entries; i++) {
				IMAGE_RESOURCE_DIRECTORY_ENTRY *entry = &first_entry_ptr[i];
				if (!pe_can_read(ctx, entry, sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY))) {
					LIBPE_WARNING("Cannot read IMAGE_RESOURCE_DIRECTORY_ENTRY");
					break;
				}

				pe_resource_node_t *new_node = pe_resource_create_node(node->depth + 1, LIBPE_RDT_DIRECTORY_ENTRY, entry, node);
				pe_resource_parse_nodes(ctx, new_node);
			}
			break;
		}
		case LIBPE_RDT_DIRECTORY_ENTRY:
		{
			const IMAGE_RESOURCE_DIRECTORY_ENTRY *entry_ptr = node->raw.directoryEntry;

			//fprintf(stderr, "DEBUG: id=%#x, dataOffset=%#x\n", entry_ptr->u0.Id, entry_ptr->u1.OffsetToData);

			pe_resource_node_t *new_node = NULL;

			// This resource has a name?
			if (entry_ptr->u0.data.NameIsString) { // entry->u0.Name & IMAGE_RESOURCE_NAME_IS_STRING
				IMAGE_RESOURCE_DATA_STRING_U *data_string_ptr = LIBPE_PTR_ADD(ctx->cached_data.resources->resource_base_ptr, entry_ptr->u0.data.NameOffset);
				if (!pe_can_read(ctx, data_string_ptr, sizeof(IMAGE_RESOURCE_DATA_STRING_U))) {
					LIBPE_WARNING("Cannot read IMAGE_RESOURCE_DATA_STRING_U");
					return NULL;
				}

				node->name = pe_resource_parse_string_u(ctx, NULL, 0, data_string_ptr);

				new_node = pe_resource_create_node(node->depth + 1, LIBPE_RDT_DATA_STRING, data_string_ptr, node);
				pe_resource_parse_nodes(ctx, new_node);
			}

			// Is it a directory?
			if (entry_ptr->u1.data.DataIsDirectory) { // entry->u1.OffsetToData & IMAGE_RESOURCE_DATA_IS_DIRECTORY
				IMAGE_RESOURCE_DIRECTORY *child_resdir_ptr = LIBPE_PTR_ADD(ctx->cached_data.resources->resource_base_ptr, entry_ptr->u1.data.OffsetToDirectory);
				if (!pe_can_read(ctx, child_resdir_ptr, sizeof(IMAGE_RESOURCE_DIRECTORY))) {
					LIBPE_WARNING("Cannot read IMAGE_RESOURCE_DIRECTORY");
					break;
				}
				new_node = pe_resource_create_node(node->depth + 1, LIBPE_RDT_RESOURCE_DIRECTORY, child_resdir_ptr, node);
			} else { // Not a directory
				IMAGE_RESOURCE_DATA_ENTRY *data_entry_ptr = LIBPE_PTR_ADD(ctx->cached_data.resources->resource_base_ptr, entry_ptr->u1.data.OffsetToDirectory);
				if (!pe_can_read(ctx, data_entry_ptr, sizeof(IMAGE_RESOURCE_DATA_ENTRY))) {
					LIBPE_WARNING("Cannot read IMAGE_RESOURCE_DATA_ENTRY");
					break;
				}
				new_node = pe_resource_create_node(node->depth + 1, LIBPE_RDT_DATA_ENTRY, data_entry_ptr, node);
			}

			pe_resource_parse_nodes(ctx, new_node);

			break;
		}
		case LIBPE_RDT_DATA_STRING:
		{
			const IMAGE_RESOURCE_DATA_STRING_U *data_string_ptr = node->raw.dataString;
			if (!pe_can_read(ctx, data_string_ptr, sizeof(IMAGE_RESOURCE_DATA_STRING_U))) {
				LIBPE_WARNING("Cannot read IMAGE_RESOURCE_DATA_STRING_U");
				break;
			}

			// TODO(jweyrich): We should store the result in the node to be useful,
			// but we still don't store specific data in the node, except for its name.
			char *buffer = pe_resource_parse_string_u(ctx, NULL, 0, data_string_ptr);
			fprintf(stderr, "DEBUG: Length=%hu, String=%s\n", data_string_ptr->Length, buffer);
			free(buffer);
			break;
		}
		case LIBPE_RDT_DATA_ENTRY:
		{
			// const IMAGE_RESOURCE_DATA_ENTRY *data_entry_ptr = node->raw.dataEntry;

			// fprintf(stderr, "DEBUG: CodePage=%u, OffsetToData=%u[%#x], Reserved=%u[%#x], Size=%u[%#x]\n",
			//	data_entry_ptr->CodePage,
			//	data_entry_ptr->OffsetToData,
			//	data_entry_ptr->OffsetToData,
			//	data_entry_ptr->Reserved,
			//	data_entry_ptr->Reserved,
			//	data_entry_ptr->Size,
			//	data_entry_ptr->Size);

			////////////////////////////////////////////////////////////////////////////////////
			// TODO(jweyrich): To be written.
			////////////////////////////////////////////////////////////////////////////////////
			break;
		}
	}

	return true;
}

static pe_resource_node_t *pe_resource_parse(pe_ctx_t *ctx, void *resource_base_ptr) {
	pe_resource_node_t *root_node = pe_resource_create_node(0, LIBPE_RDT_RESOURCE_DIRECTORY, resource_base_ptr, NULL);
	pe_resource_parse_nodes(ctx, root_node);
	//pe_resource_debug_nodes(ctx, root_node);
	return root_node;
}

static void *pe_resource_base_ptr(pe_ctx_t *ctx) {
	const IMAGE_DATA_DIRECTORY * const directory = pe_directory_by_entry(ctx, IMAGE_DIRECTORY_ENTRY_RESOURCE);
	if (directory == NULL) {
		LIBPE_WARNING("Resource directory does not exist")
		return NULL;
	}
	if (directory->VirtualAddress == 0 || directory->Size == 0) {
		LIBPE_WARNING("Resource directory VA is zero")
		return NULL;
	}
	if (directory->Size == 0) {
		LIBPE_WARNING("Resource directory size is 0")
		return NULL;
	}

	const uintptr_t offset = pe_rva2ofs(ctx, directory->VirtualAddress);
	void *ptr = LIBPE_PTR_ADD(ctx->map_addr, offset);
	if (!pe_can_read(ctx, ptr, sizeof(IMAGE_RESOURCE_DIRECTORY))) {
		LIBPE_WARNING("Cannot read IMAGE_RESOURCE_DIRECTORY");
		return NULL;
	}

	return ptr;
}

pe_resources_t *pe_resources(pe_ctx_t *ctx) {
	if (ctx->cached_data.resources != NULL)
		return ctx->cached_data.resources;

	pe_resources_t *res_ptr = calloc(1, sizeof(pe_resources_t));
	if (res_ptr == NULL) {
		// TODO: Handle allocation failure.
		abort();
	}

	ctx->cached_data.resources = res_ptr;
	ctx->cached_data.resources->err = LIBPE_E_OK;
	ctx->cached_data.resources->resource_base_ptr = pe_resource_base_ptr(ctx); // Various parts of the parsing rely on `resource_base_ptr`.
	if (ctx->cached_data.resources->resource_base_ptr != NULL) {
		ctx->cached_data.resources->root_node = pe_resource_parse(ctx, ctx->cached_data.resources->resource_base_ptr);
	}

	return ctx->cached_data.resources;
}

void pe_resources_dealloc(pe_resources_t *obj) {
	if (obj == NULL)
		return;

	pe_resource_free_nodes(obj->root_node);
	free(obj);
}
