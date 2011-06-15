/*
 * wreport/bulletin/internals - Bulletin implementation helpers
 *
 * Copyright (C) 2005--2011  ARPA-SIM <urpsim@smr.arpa.emr.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: Enrico Zini <enrico@enricozini.com>
 */

#include "internals.h"
#include "var.h"
#include "subset.h"
#include "bulletin.h"
#include <cmath>

// #define TRACE_INTERPRETER

#ifdef TRACE_INTERPRETER
#define TRACE(...) fprintf(stderr, __VA_ARGS__)
#define IFTRACE if (1)
#else
#define TRACE(...) do { } while (0)
#define IFTRACE if (0)
#endif

namespace wreport {
namespace bulletin {

Bitmap::Bitmap() : bitmap(0) {}
Bitmap::~Bitmap() {}

void Bitmap::reset()
{
    bitmap = 0;
    old_anchor = 0;
    refs.clear();
    iter = refs.rend();
}

void Bitmap::init(const Var& bitmap, const Subset& subset, unsigned anchor)
{
    this->bitmap = &bitmap;
    refs.clear();

    // From the specs it looks like bitmaps refer to all data that precedes
    // the C operator that defines or uses the bitmap, but from the data
    // samples that we have it look like when multiple bitmaps are present,
    // they always refer to the same set of variables. For this reason we
    // remember the first anchor point that we see and always refer the
    // other bitmaps that we see to it.
    if (old_anchor)
        anchor = old_anchor;
    else
        old_anchor = anchor;

    unsigned b_cur = bitmap.info()->len;
    unsigned s_cur = anchor;
    if (b_cur == 0) throw error_consistency("data present bitmap has length 0");
    if (s_cur == 0) throw error_consistency("data present bitmap is anchored at start of subset");

    while (true)
    {
        --b_cur;
        --s_cur;
        while (WR_VAR_F(subset[s_cur].code()) != 0)
        {
            if (s_cur == 0) throw error_consistency("bitmap refers to variables before the start of the subset");
            --s_cur;
        }

        if (bitmap.value()[b_cur] == '+')
            refs.push_back(s_cur);

        if (b_cur == 0)
            break;
        if (s_cur == 0)
            throw error_consistency("bitmap refers to variables before the start of the subset");
    }

    iter = refs.rbegin();
}

bool Bitmap::eob() const { return iter == refs.rend(); }
unsigned Bitmap::next() { unsigned res = *iter; ++iter; return res; }



Visitor::Visitor() : btable(0), current_subset(0) {}
Visitor::~Visitor() {}

Varinfo Visitor::get_varinfo(Varcode code)
{
    Varinfo peek = btable->query(code);

    if (!c_scale_change && !c_width_change && !c_string_len_override)
        return peek;

    int scale = peek->scale;
    if (c_scale_change)
    {
        TRACE("get_info:applying %d scale change\n", c_scale_change);
        scale += c_scale_change;
    }

    int bit_len = peek->bit_len;
    if (peek->is_string() && c_string_len_override)
    {
        TRACE("get_info:overriding string to %d bytes\n", c_string_len_override);
        bit_len = c_string_len_override * 8;
    }
    else if (c_width_change)
    {
        TRACE("get_info:applying %d width change\n", c_width_change);
        bit_len += c_width_change;
    }

    TRACE("get_info:requesting alteration scale:%d, bit_len:%d\n", scale, bit_len);
    return btable->query_altered(code, scale, bit_len);
}

void Visitor::b_variable(Varcode code)
{
    Varinfo info = get_varinfo(code);
    // Choose which value we should encode
    if (WR_VAR_F(code) == 0 && WR_VAR_X(code) == 33 && !bitmap.eob())
    {
        // Attribute of the variable pointed by the bitmap
        unsigned target = bitmap.next();
        TRACE("Encode attribute %01d%02d%03d subset pos %u\n",
                WR_VAR_F(code), WR_VAR_X(code), WR_VAR_Y(code), target);
        do_attr(info, target, code);
    } else {
        // Proper variable
        TRACE("Encode variable %01d%02d%03d\n",
                WR_VAR_F(info->var), WR_VAR_X(info->var), WR_VAR_Y(info->var));
        if (c04_bits > 0)
            do_associated_field(c04_bits, c04_meaning);
        do_var(info);
        ++data_pos;
    }
}

void Visitor::c_modifier(Varcode code)
{
    TRACE("C DATA %01d%02d%03d\n", WR_VAR_F(code), WR_VAR_X(code), WR_VAR_Y(code));
}

void Visitor::c_change_data_width(Varcode code, int change)
{
    TRACE("Set width change from %d to %d\n", c_width_change, change);
    c_width_change = change;
}

void Visitor::c_change_data_scale(Varcode code, int change)
{
    TRACE("Set scale change from %d to %d\n", c_scale_change, change);
    c_scale_change = change;
}

void Visitor::c_associated_field(Varcode code, Varcode sig_code, unsigned nbits)
{
    // Add associated field
    TRACE("Set C04 bits to %d\n", WR_VAR_Y(code));
    // FIXME: nested C04 modifiers are not currently implemented
    if (WR_VAR_Y(code) && c04_bits)
        throw error_unimplemented("nested C04 modifiers are not yet implemented");
    if (WR_VAR_Y(code) > 32)
        error_unimplemented::throwf("C04 modifier wants %d bits but only at most 32 are supported", WR_VAR_Y(code));
    if (WR_VAR_Y(code))
    {
        // Get encoding informations for this associated_field_significance
        Varinfo info = btable->query(WR_VAR(0, 31, 21));

        // Encode B31021
        const Var& var = do_semantic_var(info);
        c04_meaning = var.enqi();
        ++data_pos;
    }
    c04_bits = WR_VAR_Y(code);
}

void Visitor::c_char_data(Varcode code)
{
    do_char_data(code);
}

void Visitor::c_local_descriptor(Varcode code, Varcode desc_code, unsigned nbits)
{
    // Length of next local descriptor
    if (WR_VAR_Y(code) > 32)
        error_unimplemented::throwf("C06 modifier found for %d bits but only at most 32 are supported", WR_VAR_Y(code));
    if (WR_VAR_Y(code))
    {
        bool skip = true;
        if (btable->contains(desc_code))
        {
            Varinfo info = get_varinfo(desc_code);
            if (info->bit_len == WR_VAR_Y(code))
            {
                // If we can resolve the descriptor and the size is the
                // same, attempt decoding
                do_var(info);
                skip = false;
            }
        }
        if (skip)
        {
            MutableVarinfo info(MutableVarinfo::create_singleuse());
            info->set(code, "UNKNOWN LOCAL DESCRIPTOR", "UNKNOWN", 0, 0,
                    ceil(log10(exp2(WR_VAR_Y(code)))), 0, WR_VAR_Y(code), VARINFO_FLAG_STRING);
            do_var(info);
        }
        ++data_pos;
    }
}

void Visitor::c_char_data_override(Varcode code, unsigned new_length)
{
    IFTRACE {
        if (new_length)
            TRACE("decode_c_data:character size overridden to %d chars for all fields\n", new_length);
        else
            TRACE("decode_c_data:character size overridde end\n");
    }
    c_string_len_override = new_length;
}

void Visitor::c_quality_information_bitmap(Varcode code)
{
    // Quality information
    if (WR_VAR_Y(code) != 0)
        error_consistency::throwf("C modifier %d%02d%03d not yet supported",
                    WR_VAR_F(code),
                    WR_VAR_X(code),
                    WR_VAR_Y(code));
    want_bitmap = true;
}

void Visitor::c_substituted_value_bitmap(Varcode code)
{
    want_bitmap = true;
}

void Visitor::c_substituted_value(Varcode code)
{
    if (bitmap.bitmap == NULL)
        error_consistency::throwf("found C23255 with no active bitmap");
    if (bitmap.eob())
        error_consistency::throwf("found C23255 while at the end of active bitmap");
    unsigned target = bitmap.next();
    // Use the details of the corrisponding variable for decoding
    Varinfo info = (*current_subset)[target].info();
    // Encode the value
    do_attr(info, target, info->var);
}

/* If using delayed replication and count is not -1, use count for the delayed
 * replication factor; else, look for a delayed replication factor among the
 * input variables */
void Visitor::r_replication(Varcode code, Varcode delayed_code, const Opcodes& ops)
{
    //int group = WR_VAR_X(code);
    unsigned count = WR_VAR_Y(code);

    IFTRACE{
        TRACE("bufr_message_encode_r_data %01d%02d%03d %d %d: items: ",
                WR_VAR_F(ops.head()), WR_VAR_X(ops.head()), WR_VAR_Y(ops.head()), group, count);
        ops.print(stderr);
        TRACE("\n");
    }

    if (want_bitmap)
    {
        if (count == 0 && delayed_code == 0)
            delayed_code = WR_VAR(0, 31, 12);
        const Var* bitmap_var = do_bitmap(code, delayed_code, ops);
        bitmap.init(*bitmap_var, *current_subset, data_pos);
        if (delayed_code)
            ++data_pos;
        want_bitmap = false;
    } else {
        if (count == 0)
        {
            Varinfo info = btable->query(delayed_code ? delayed_code : WR_VAR(0, 31, 12));
            Var var = do_semantic_var(info);
            count = var.enqi();
            ++data_pos;
        }
        TRACE("encode_r_data %d items %d times%s\n", group, count, delayed_code ? " (delayed)" : "");
        IFTRACE {
            TRACE("Repeat opcodes: ");
            ops.print(stderr);
            TRACE("\n");
        }

        // encode_data_section on it `count' times
        for (unsigned i = 0; i < count; ++i)
        {
            do_start_repetition(i);
            ops.visit(*this);
        }
    }
}

void Visitor::do_start_subset(unsigned subset_no, const Subset& current_subset)
{
    TRACE("visit: start encoding subset %u\n", subset_no);

    this->current_subset = &current_subset;

    c_scale_change = 0;
    c_width_change = 0;
    c_string_len_override = 0;
    bitmap.reset();
    c04_bits = 0;
    c04_meaning = 63;
    want_bitmap = false;
    data_pos = 0;
}

void Visitor::do_start_repetition(unsigned idx) {}



BaseVisitor::BaseVisitor(Bulletin& bulletin)
    : bulletin(bulletin), current_subset_no(0)
{
}

Var& BaseVisitor::get_var()
{
    Var& res = get_var(current_var);
    ++current_var;
    return res;
}

Var& BaseVisitor::get_var(unsigned var_pos) const
{
    unsigned max_var = current_subset->size();
    if (var_pos >= max_var)
        error_consistency::throwf("requested variable #%u out of a maximum of %u in subset %u",
                var_pos, max_var, current_subset_no);
    return bulletin.subsets[current_subset_no][var_pos];
}

void BaseVisitor::do_start_subset(unsigned subset_no, const Subset& current_subset)
{
    Visitor::do_start_subset(subset_no, current_subset);
    if (subset_no >= bulletin.subsets.size())
        error_consistency::throwf("requested subset #%u out of a maximum of %zd", subset_no, bulletin.subsets.size());
    this->current_subset = &(bulletin.subsets[subset_no]);
    current_subset_no = subset_no;
    current_var = 0;
}

const Var* BaseVisitor::do_bitmap(Varcode code, Varcode delayed_code, const Opcodes& ops)
{
    const Var& var = get_var();
    if (WR_VAR_F(var.code()) != 2)
        error_consistency::throwf("variable at %u is %01d%02d%03d and not a data present bitmap",
                current_var-1, WR_VAR_F(var.code()), WR_VAR_X(var.code()), WR_VAR_Y(var.code()));
    return &var;
}


ConstBaseVisitor::ConstBaseVisitor(const Bulletin& bulletin)
    : bulletin(bulletin), current_subset_no(0)
{
}

const Var& ConstBaseVisitor::get_var()
{
    const Var& res = get_var(current_var);
    ++current_var;
    return res;
}

const Var& ConstBaseVisitor::get_var(unsigned var_pos) const
{
    unsigned max_var = current_subset->size();
    if (var_pos >= max_var)
        error_consistency::throwf("requested variable #%u out of a maximum of %u in subset %u",
                var_pos, max_var, current_subset_no);
    return (*current_subset)[var_pos];
}

void ConstBaseVisitor::do_start_subset(unsigned subset_no, const Subset& current_subset)
{
    Visitor::do_start_subset(subset_no, current_subset);
    if (subset_no >= bulletin.subsets.size())
        error_consistency::throwf("requested subset #%u out of a maximum of %zd", subset_no, bulletin.subsets.size());
    current_subset_no = subset_no;
    current_var = 0;
}

const Var* ConstBaseVisitor::do_bitmap(Varcode code, Varcode delayed_code, const Opcodes& ops)
{
    const Var& var = get_var();
    if (WR_VAR_F(var.code()) != 2)
        error_consistency::throwf("variable at %u is %01d%02d%03d and not a data present bitmap",
                current_var-1, WR_VAR_F(var.code()), WR_VAR_X(var.code()), WR_VAR_Y(var.code()));
    return &var;
}

}
}