#include "./pedersen.hpp"

#include "../../../composer/turbo_composer.hpp"

#include "../../field/field.hpp"
#include "../../group/group_utils.hpp"

#include "../crypto.hpp"

namespace plonk {
namespace stdlib {
namespace pedersen {
typedef field_t<waffle::TurboComposer> field_t;
using namespace barretenberg;


point hash_single(const field_t& in, const size_t hash_index)
{
    field_t scalar = in;
    if (!fr::eq(in.additive_constant, fr::zero) || !fr::eq(in.multiplicative_constant, fr::one)) {
        scalar = scalar.normalize();
    }
    waffle::TurboComposer* ctx = in.context;
    ASSERT(ctx != nullptr);
    fr::field_t scalar_multiplier = fr::from_montgomery_form(scalar.get_value());

    constexpr size_t num_bits = 254;
    constexpr size_t num_quads_base = (num_bits - 1) >> 1;
    constexpr size_t num_quads = ((num_quads_base << 1) + 1 < num_bits) ? num_quads_base + 1 : num_quads_base;
    constexpr size_t num_wnaf_bits = (num_quads << 1) + 1;

    constexpr size_t initial_exponent = ((num_bits & 1) == 1) ? num_bits - 1: num_bits;
    const plonk::stdlib::group_utils::fixed_base_ladder* ladder =
        plonk::stdlib::group_utils::get_hash_ladder(hash_index, num_bits);
    grumpkin::g1::affine_element generator = plonk::stdlib::group_utils::get_generator(hash_index * 2 + 1);

    grumpkin::g1::element origin_points[2];
    grumpkin::g1::affine_to_jacobian(ladder[0].one, origin_points[0]);
    grumpkin::g1::mixed_add(origin_points[0], generator, origin_points[1]);
    origin_points[1] = grumpkin::g1::normalize(origin_points[1]);

    fr::field_t scalar_multiplier_base = fr::to_montgomery_form(scalar_multiplier);

    if ((scalar_multiplier.data[0] & 1) == 0) {
        fr::field_t two = fr::add(fr::one, fr::one);
        scalar_multiplier_base = fr::sub(scalar_multiplier_base, two);
    }
    scalar_multiplier_base = fr::from_montgomery_form(scalar_multiplier_base);
    uint64_t wnaf_entries[num_quads + 1] = { 0 };
    bool skew = false;

    barretenberg::wnaf::fixed_wnaf<num_wnaf_bits, 1, 2>(&scalar_multiplier_base.data[0], &wnaf_entries[0], skew, 0);

    fr::field_t accumulator_offset = fr::invert(fr::pow_small(fr::add(fr::one, fr::one), initial_exponent));
    
    fr::field_t origin_accumulators[2]{ fr::one, fr::add(accumulator_offset, fr::one) };

    grumpkin::g1::element* multiplication_transcript =
        static_cast<grumpkin::g1::element*>(aligned_alloc(64, sizeof(grumpkin::g1::element) * (num_quads + 1)));
    fr::field_t* accumulator_transcript =
        static_cast<fr::field_t*>(aligned_alloc(64, sizeof(fr::field_t) * (num_quads + 1)));

    if (skew) {
        multiplication_transcript[0] = origin_points[1];
        accumulator_transcript[0] = origin_accumulators[1];
    } else {
        multiplication_transcript[0] = origin_points[0];
        accumulator_transcript[0] = origin_accumulators[0];
    }
    fr::field_t one = fr::one;
    fr::field_t three = fr::add(fr::add(one, one), one);
    
    for (size_t i = 0; i < num_quads; ++i) {
        uint64_t entry = wnaf_entries[i + 1] & 0xffffff;

        fr::field_t prev_accumulator = fr::add(accumulator_transcript[i], accumulator_transcript[i]);
        prev_accumulator = fr::add(prev_accumulator, prev_accumulator);

        grumpkin::g1::affine_element point_to_add = (entry == 1) ? ladder[i + 1].three : ladder[i + 1].one;

        fr::field_t scalar_to_add = (entry == 1) ? three : one;
        uint64_t predicate = (wnaf_entries[i + 1] >> 31U) & 1U;
        if (predicate) {
            grumpkin::g1::__neg(point_to_add, point_to_add);
            fr::__neg(scalar_to_add, scalar_to_add);
        }
        accumulator_transcript[i + 1] = fr::add(prev_accumulator, scalar_to_add);
        grumpkin::g1::mixed_add(multiplication_transcript[i], point_to_add, multiplication_transcript[i + 1]);
    }

    grumpkin::g1::batch_normalize(&multiplication_transcript[0], num_quads + 1);

    waffle::fixed_group_init_quad init_quad{ origin_points[0].x,
                                             fr::sub(origin_points[0].x, origin_points[1].x),
                                             origin_points[0].y,
                                             fr::sub(origin_points[0].y, origin_points[1].y) };

    fr::field_t x_alpha = accumulator_offset;
    for (size_t i = 0; i < num_quads; ++i) {
        waffle::fixed_group_add_quad round_quad;
        round_quad.d = ctx->add_variable(accumulator_transcript[i]);
        round_quad.a = ctx->add_variable(multiplication_transcript[i].x);
        round_quad.b = ctx->add_variable(multiplication_transcript[i].y);

        if (i == 0)
        {
            // we need to ensure that the first value of x_alpha is a defined constant.
            // However, repeated applications of the pedersen hash will use the same constant value.
            // `put_constant_variable` will create a gate that fixes the value of x_alpha, but only once
            round_quad.c = ctx->put_constant_variable(x_alpha);
        }
        else
        {
            round_quad.c = ctx->add_variable(x_alpha);
        }
        if ((wnaf_entries[i + 1] & 0xffffffU) == 0) {
            x_alpha = ladder[i + 1].one.x;
        } else {
            x_alpha = ladder[i + 1].three.x;
        }
        round_quad.q_x_1 = ladder[i + 1].q_x_1;
        round_quad.q_x_2 = ladder[i + 1].q_x_2;
        round_quad.q_y_1 = ladder[i + 1].q_y_1;
        round_quad.q_y_2 = ladder[i + 1].q_y_2;

        if (i > 0) {
            ctx->create_fixed_group_add_gate(round_quad);
        } else {
            ctx->create_fixed_group_add_gate_with_init(round_quad, init_quad);
        }
    }

    waffle::add_quad add_quad{ ctx->add_variable(multiplication_transcript[num_quads].x),
                               ctx->add_variable(multiplication_transcript[num_quads].y),
                               ctx->add_variable(x_alpha),
                               ctx->add_variable(accumulator_transcript[num_quads]),
                               fr::zero,
                               fr::zero,
                               fr::zero,
                               fr::zero,
                               fr::zero };
    ctx->create_big_add_gate(add_quad);

    point result;
    result.x = field_t(ctx);
    result.x.witness_index = add_quad.a;
    result.y = field_t(ctx);
    result.y.witness_index = add_quad.b;

    ctx->assert_equal(add_quad.d, in.witness_index);
    return result;
}

field_t compress(const field_t& in_left, const field_t& in_right)
{
    point first = hash_single(in_left, 0);
    point second = hash_single(in_right, 1);

    // combine hash limbs
    // TODO: replace this addition with a variable-base custom gate
    field_t lambda = (second.y - first.y) / (second.x - first.x);
    field_t x_3 = lambda * lambda - second.x - first.x;
    return x_3;
}

} // namespace pedersen
} // namespace stdlib
} // namespace plonk