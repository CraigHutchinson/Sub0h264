/** Spec-only Reference Decoder -- Unity Build
 *
 *  Single compilation unit that includes all spec reference headers.
 *  This ensures all templates and inline functions are instantiated.
 *
 *  SPDX-License-Identifier: MIT
 */

#include "spec_ref_cabac.hpp"
#include "spec_ref_cabac_init.hpp"
#include "spec_ref_tables.hpp"
#include "spec_ref_transform.hpp"
#include "spec_ref_intra_pred.hpp"
#include "spec_ref_cabac_parse.hpp"
#include "spec_ref_decode.hpp"
