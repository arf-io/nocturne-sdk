#include "simulation_internal.h"
#include "spatial_geometry_internal.h"
#include "simulation_wide_internal.h"
#include <float.h>

#define BODY_LOAD_LIMIT 1e38
#define BODY_TURN_LIMIT .25
#define BODY_TINY_ROTATION (2*DBL_MIN)
#define BODY_TINY_COMPOSITION 0x1p-1000
#define BODY_PI 0x1.921fb54442d18p+1

/* Twice the quaternion vector permits a subnormal axis-angle magnitude. */
typedef struct BodyRotation { double scalar; NtSpatialExact twice_vector[3]; } BodyRotation;
typedef struct RotationVector { NtVec3 axis; double magnitude; } RotationVector;
typedef struct StartingRotation { NtVec3 axis; double angle; NtSpatialExact reduced_angle; } StartingRotation;

static bool valid_load(NtVec3 load)
{
    return fabs(load.x)<=BODY_LOAD_LIMIT && fabs(load.y)<=BODY_LOAD_LIMIT && fabs(load.z)<=BODY_LOAD_LIMIT;
}

static bool bounded_quotient(const NtSpatialExact *numerator, const NtSpatialExact *denominator, double *out)
{
    NtSpatialExact magnitude;
    nt_spatial_exact_copy(&magnitude,numerator);
    magnitude.negative = false;
    if (nt_spatial_exact_compare_scaled(&magnitude,denominator,NT_SIMULATION_COORDINATE_LIMIT)>0)
        return false;
    *out = nt_spatial_exact_quotient(numerator,denominator);
    return true;
}

static bool move_body(const NtFloatingBody *body, NtVec3 force, double elapsed, NtFloatingBody *result)
{
    NtSpatialExact time, velocity_unit, twice_micros, position_unit, two;
    nt_spatial_exact_number(&time,elapsed,0);
    nt_spatial_exact_number(&two,2,0);
    nt_simulation_product(&velocity_unit,body->mass,1000000);
    nt_spatial_exact_number(&twice_micros,2000000,0);
    nt_spatial_exact_multiply(&position_unit,&velocity_unit,&twice_micros);
    const double p[3] = {body->position.x,body->position.y,body->position.z};
    const double v[3] = {body->linear_velocity.x,body->linear_velocity.y,body->linear_velocity.z};
    const double f[3] = {force.x,force.y,force.z};
    double position[3], velocity[3];
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact value, old_velocity, impulse, new_velocity;
        nt_spatial_exact_number(&value,v[i],0);
        nt_spatial_exact_multiply(&old_velocity,&value,&velocity_unit);
        nt_simulation_product(&impulse,f[i],elapsed);
        nt_spatial_exact_add(&new_velocity,&old_velocity,&impulse);
        if (!bounded_quotient(&new_velocity,&velocity_unit,&velocity[i])) return false;

        NtSpatialExact displacement, average, travel, old_position, new_position;
        nt_spatial_exact_multiply(&displacement,&old_velocity,&two);
        nt_spatial_exact_add(&average,&displacement,&impulse);
        nt_spatial_exact_multiply(&travel,&average,&time);
        nt_spatial_exact_number(&value,p[i],0);
        nt_spatial_exact_multiply(&old_position,&value,&position_unit);
        nt_spatial_exact_add(&new_position,&old_position,&travel);
        if (!bounded_quotient(&new_position,&position_unit,&position[i])) return false;
    }
    result->position = (NtVec3){position[0],position[1],position[2]};
    result->linear_velocity = (NtVec3){velocity[0],velocity[1],velocity[2]};
    return true;
}

static void rotate_transposed(const NtSpatialTransform *pose, NtVec3 value, NtSpatialExact out[3])
{
    NtSpatialExact vector[3];
    nt_simulation_exact_vec(value,vector);
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact column[3];
        nt_simulation_exact_vec((NtVec3){pose->linear[0][i],pose->linear[1][i],pose->linear[2][i]},column);
        nt_exact_dot(column,vector,&out[i]);
    }
}

static NT_SIMULATION_NOINLINE NtResult angular_acceleration(const NtFloatingBody *body, NtVec3 torque,
                                      NtSpatialExact out[3], NtSpatialExact *unit)
{
    const NtVec3 inertia = body->principal_inertia;
    if (inertia.x==inertia.y && inertia.x==inertia.z) {
        nt_simulation_exact_vec(torque,out);
        nt_spatial_exact_number(unit,inertia.x,0);
        return NT_RESULT_OK;
    }
    NtSpatialTransform pose;
    const NtResult status = nt_floating_body_transform(body,&pose);
    if (status!=NT_RESULT_OK) return status;
    NtSpatialExact moments[3], omega[3], applied[3], alpha[3];
    nt_simulation_exact_vec(inertia,moments);
    rotate_transposed(&pose,body->angular_velocity,omega);
    rotate_transposed(&pose,torque,applied);

    NtSpatialExact pair;
    nt_spatial_exact_multiply(&pair,&moments[0],&moments[1]);
    nt_spatial_exact_multiply(unit,&pair,&moments[2]);
    for (unsigned i=0;i<3;++i) {
        const unsigned j = (i+1)%3, k = (i+2)%3;
        NtSpatialExact difference, speed, gyro, net;
        nt_spatial_exact_subtract(&difference,&moments[k],&moments[j]);
        nt_spatial_exact_multiply(&speed,&omega[j],&omega[k]);
        nt_spatial_exact_multiply(&gyro,&difference,&speed);
        nt_spatial_exact_subtract(&net,&applied[i],&gyro);
        nt_spatial_exact_multiply(&pair,&moments[j],&moments[k]);
        nt_spatial_exact_multiply(&alpha[i],&net,&pair);
    }
    for (unsigned i=0;i<3;++i) {
        const double *r = pose.linear[i];
        NtSpatialExact row[3];
        nt_simulation_exact_vec((NtVec3){r[0],r[1],r[2]},row);
        nt_exact_dot(row,alpha,&out[i]);
    }
    return NT_RESULT_OK;
}

static void body_quaternion(NtVec3 axis, double angle, BodyRotation *out)
{
    const bool tiny = fabs(angle)<BODY_TINY_ROTATION;
    const double sine = tiny ? angle : 2*sin(.5*angle);
    out->scalar = tiny ? 1 : cos(.5*angle);
    nt_simulation_product(&out->twice_vector[0],sine,axis.x);
    nt_simulation_product(&out->twice_vector[1],sine,axis.y);
    nt_simulation_product(&out->twice_vector[2],sine,axis.z);
}

static NT_SIMULATION_NOINLINE void compose_rotation(const BodyRotation *outer, const NtSpatialExact *outer_unit,
                             const BodyRotation *inner, BodyRotation *out)
{
    /* The vector for the outer rotation has a common denominator.
     * Carry it through composition so cancellation can resolve a midpoint. */
    const NtSpatialExact *a = outer->twice_vector, *b = inner->twice_vector;
    NtSpatialExact cross[3], dot, half, quarter, scalar, wa, wb;
    nt_exact_cross(a,b,cross);
    nt_exact_dot(a,b,&dot);
    nt_spatial_exact_number(&half,.5,0);
    nt_spatial_exact_number(&quarter,.25,0);
    nt_spatial_exact_number(&scalar,outer->scalar,0);
    nt_spatial_exact_multiply(&wa,&scalar,outer_unit);
    nt_spatial_exact_number(&wb,inner->scalar,0);

    NtSpatialExact product, weighted;
    nt_spatial_exact_multiply(&product,&wa,&wb);
    nt_spatial_exact_multiply(&weighted,&dot,&quarter);
    nt_spatial_exact_subtract(&scalar,&product,&weighted);
    out->scalar = nt_spatial_exact_quotient(&scalar,outer_unit);
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact first, second;
        nt_spatial_exact_multiply(&first,&wa,&b[i]);
        nt_spatial_exact_multiply(&second,&wb,&a[i]);
        nt_spatial_exact_add(&out->twice_vector[i],&first,&second);
        nt_spatial_exact_multiply(&weighted,&cross[i],&half);
        nt_simulation_accumulate(&out->twice_vector[i],&weighted);
    }
}

static NT_SIMULATION_NOINLINE NtResult rotation_vector(const NtSpatialExact n[3], const NtSpatialExact *unit, RotationVector *out)
{
    bool nonzero = false;
    int highest = 0;
    for (unsigned i=0;i<3;++i) {
        if (!n[i].count) continue;
        const int end = n[i].offset+(int)n[i].count;
        if (!nonzero || end>highest) highest = end;
        nonzero = true;
    }
    *out = (RotationVector){0};
    if (!nonzero) return NT_RESULT_OK;
    const int shift = highest-unit->offset-(int)unit->count;
    double component[3];
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact scaled;
        nt_spatial_exact_copy(&scaled,&n[i]);
        scaled.offset -= shift;
        component[i] = nt_spatial_exact_quotient(&scaled,unit);
    }
    const NtVec3 scaled = {component[0],component[1],component[2]};
    const double length = hypot(hypot(component[0],component[1]),component[2]);
    out->magnitude = nt_simulation_round_norm(n,unit,scalbn(length,32*shift));
    return nt_spatial_vec_normalize(scaled,&out->axis);
}

static NT_SIMULATION_NOINLINE void starting_rotation(const NtFloatingBody *body, StartingRotation *out)
{
    out->axis = body->rotation_axis;
    out->angle = body->rotation_radians;
    nt_spatial_exact_number(&out->reduced_angle,out->angle,0);
    const double turns = nearbyint(out->angle/(2*BODY_PI));
    if (!turns) return;

    /* The first 384 fractional bits of 2*pi, low word first. Subtract
     * whole turns before rounding the small remainder to binary64. */
    static const NtSpatialExact period = {
        .limbs = {0x69d218d9,0x7ca8cd9e,0x71a026ef,0x8a5043cc,0xd89cd912,0x105df531,
                  0x533e63a0,0x48127044,0x06e0e689,0x2633145c,0x0b4611a6,0x487ed511,6},
        .offset = -12, .count = 13
    };
    NtSpatialExact count, whole, angle, one;
    nt_spatial_exact_number(&count,turns,0);
    nt_spatial_exact_multiply(&whole,&period,&count);
    nt_spatial_exact_copy(&angle,&out->reduced_angle);
    nt_spatial_exact_subtract(&out->reduced_angle,&angle,&whole);
    nt_spatial_exact_number(&one,1,0);
    out->angle = nt_spatial_exact_quotient(&out->reduced_angle,&one);
}

static NT_SIMULATION_NOINLINE NtResult compose_tiny_rotation(const NtSpatialExact turn[3], const NtSpatialExact *unit,
                                      const StartingRotation *initial, NtFloatingBody *result)
{
    NtSpatialExact vector[3];
    nt_simulation_tiny_vector(turn,initial->axis,initial->angle,unit,vector);
    RotationVector direction;
    const NtResult status = rotation_vector(vector,unit,&direction);
    if (status!=NT_RESULT_OK) return status;
    result->rotation_radians = nt_simulation_round_tiny_angle(turn,initial->axis,initial->angle,
                                                             unit,direction.magnitude);
    result->rotation_axis = result->rotation_radians ? direction.axis : (NtVec3){0,1,0};
    return NT_RESULT_OK;
}

static NT_SIMULATION_NOINLINE bool compose_parallel_rotation(const StartingRotation *initial, const NtSpatialExact n[3],
                                      const NtSpatialExact *unit, RotationVector motion, NtFloatingBody *result)
{
    const double angle = fabs(initial->angle);
    if (angle>BODY_PI-BODY_TURN_LIMIT) return false;
    NtSpatialExact axis[3], cross[3], dot;
    nt_simulation_exact_vec(initial->axis,axis);
    nt_exact_cross(n,axis,cross);
    if (cross[0].count || cross[1].count || cross[2].count) return false;
    nt_exact_dot(n,axis,&dot);
    const bool opposite = initial->reduced_angle.negative!=dot.negative;
    bool reversed;
    const double magnitude = nt_simulation_round_parallel_angle(n,unit,&initial->reduced_angle,
                                                               angle+motion.magnitude,opposite,&reversed);
    result->rotation_radians = magnitude;
    const double sign = reversed ? -1 : 1;
    result->rotation_axis = magnitude ? (NtVec3){sign*motion.axis.x,sign*motion.axis.y,sign*motion.axis.z} :
                                        (NtVec3){0,1,0};
    return true;
}

static void scaled_quotient(NtSpatialExact *out, const NtSpatialExact *n, const NtSpatialExact *d)
{
    NtSpatialExact scaled;
    nt_spatial_exact_copy(&scaled,n);
    const int shift = n->offset+(int)n->count-d->offset-(int)d->count;
    scaled.offset -= shift;
    nt_spatial_exact_number(out,nt_spatial_exact_quotient(&scaled,d),32*shift);
}

static NT_SIMULATION_NOINLINE void compose_near_opposite(const StartingRotation *start, const NtSpatialExact n[3],
                                  const NtSpatialExact *unit, RotationVector motion,
                                  double alignment, NtSpatialExact *residual, BodyRotation *out)
{
    const NtVec3 raw_axis = start->axis;
    const double scale = fmax(fabs(raw_axis.x),fmax(fabs(raw_axis.y),fabs(raw_axis.z)));
    const double length = hypot(hypot(raw_axis.x/scale,raw_axis.y/scale),raw_axis.z/scale);
    const double initial = fabs(start->angle);
    const double sine = sin(.5*motion.magnitude), cosine = cos(.5*motion.magnitude);
    const double old_sine = sin(.5*initial), old_cosine = cos(.5*initial);
    NtSpatialExact axis[3], direction[3], cross[3], sine_axis[3], tangent[3];
    nt_simulation_exact_vec(raw_axis,axis);
    nt_simulation_exact_vec(motion.axis,direction);
    nt_exact_cross(n,axis,cross);
    NtSpatialExact axis_scale, angle_unit, cross_unit, one;
    nt_simulation_product(&axis_scale,scale,length);
    nt_spatial_exact_number(&one,motion.magnitude,0);
    nt_spatial_exact_multiply(&angle_unit,unit,&one);
    nt_spatial_exact_multiply(&cross_unit,&angle_unit,&axis_scale);
    nt_spatial_exact_number(&one,1,0);
    double components[3];
    for (unsigned i=0;i<3;++i) {
        if (!cross[i].count) {
            nt_spatial_exact_number(&sine_axis[i],0,0);
        } else {
            cross[i].negative ^= !start->reduced_angle.negative;
            scaled_quotient(&sine_axis[i],&cross[i],&cross_unit);
        }
        components[i] = nt_spatial_exact_quotient(&sine_axis[i],&one);
    }
    nt_exact_cross(direction,sine_axis,tangent);
    const double arc = (components[0]*components[0]+components[1]*components[1]+components[2]*components[2])/(1+alignment);
    NtSpatialExact along, across, offset, twist;
    const double difference = nt_spatial_exact_quotient(residual,&one);
    if (fabs(difference)>=BODY_TINY_ROTATION)
        nt_spatial_exact_number(residual,2*sin(.5*difference),0);
    /* If u and b are the nearly parallel motion and reversed body axes,
     * u-b = u×(u×b) + (|u×b|^2/(1+u·b))*u. This keeps the small change
     * from subtracting large quaternion components. */
    nt_spatial_exact_number(&across,2*cosine*old_sine,0);
    nt_spatial_exact_number(&twist,-2*sine*old_sine,0);
    nt_spatial_exact_number(&offset,arc,0);
    nt_spatial_exact_multiply(&along,&across,&offset);
    nt_simulation_accumulate(&along,residual);
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact component;
        nt_spatial_exact_multiply(&out->twice_vector[i],&along,&direction[i]);
        nt_spatial_exact_multiply(&component,&across,&tangent[i]);
        nt_simulation_accumulate(&out->twice_vector[i],&component);
        nt_spatial_exact_multiply(&component,&twist,&sine_axis[i]);
        nt_simulation_accumulate(&out->twice_vector[i],&component);
    }
    out->scalar = cosine*old_cosine+sine*old_sine*alignment;
}

static NT_SIMULATION_NOINLINE void compose_ordinary_rotation(
    const NtSpatialExact n[3], const NtSpatialExact *unit, RotationVector motion,
    NtVec3 axis, double angle, BodyRotation *combined)
{
    BodyRotation delta, initial;
    if (motion.magnitude<BODY_TINY_ROTATION) {
        /* At this scale halving the angle can already round. Use the
         * linear sine and unit cosine with the full increment. */
        delta.scalar = 1;
        for (unsigned i=0;i<3;++i) nt_spatial_exact_copy(&delta.twice_vector[i],&n[i]);
    } else body_quaternion(motion.axis,motion.magnitude,&delta);
    body_quaternion(axis,angle,&initial);
    compose_rotation(&delta,unit,&initial,combined);
}

static NT_SIMULATION_NOINLINE NtResult compose_body_rotation(
    const StartingRotation *start_rotation, const NtSpatialExact n[3], const NtSpatialExact *unit,
    RotationVector motion, NtFloatingBody *result)
{
    NtVec3 axis;
    NtResult status = nt_spatial_vec_normalize(start_rotation->axis,&axis);
    if (status!=NT_RESULT_OK) return status;
    const double start = fabs(start_rotation->angle);

    BodyRotation combined;
    NtSpatialExact one;
    nt_spatial_exact_number(&one,1,0);
    const NtSpatialExact *delta_unit = &one;
    const double sign = start_rotation->reduced_angle.negative ? 1 : -1;
    const double alignment = sign*(axis.x*motion.axis.x+axis.y*motion.axis.y+axis.z*motion.axis.z);
    if (motion.magnitude>=BODY_TINY_ROTATION && start>=BODY_TINY_ROTATION &&
        alignment>.5 && start>=.5*motion.magnitude && start<=2*motion.magnitude) {
        NtSpatialExact residual;
        nt_simulation_angle_residual(n,unit,&start_rotation->reduced_angle,start+motion.magnitude,&residual);
        compose_near_opposite(start_rotation,n,unit,motion,alignment,&residual,&combined);
    } else {
        if (motion.magnitude<BODY_TINY_ROTATION) delta_unit = unit;
        compose_ordinary_rotation(n,delta_unit,motion,axis,start_rotation->angle,&combined);
    }

    RotationVector rotation;
    status = rotation_vector(combined.twice_vector,delta_unit,&rotation);
    if (status!=NT_RESULT_OK) return status;
    const double magnitude = rotation.magnitude;
    if (!magnitude) {
        result->rotation_axis = (NtVec3){0,1,0};
        result->rotation_radians = 0;
    } else {
        const double orientation = combined.scalar<0 ? -1 : 1;
        result->rotation_axis = (NtVec3){orientation*rotation.axis.x,orientation*rotation.axis.y,orientation*rotation.axis.z};
        result->rotation_radians = magnitude<BODY_TINY_ROTATION ? magnitude : 2*atan2(.5*magnitude,fabs(combined.scalar));
    }
    return NT_RESULT_OK;
}

static NtResult rotate_body(const NtFloatingBody *body, const NtSpatialExact n[3], const NtSpatialExact *unit,
                             NtFloatingBody *result)
{
    if (!n[0].count && !n[1].count && !n[2].count) return NT_RESULT_OK;
    RotationVector motion;
    const NtResult status = rotation_vector(n,unit,&motion);
    if (status!=NT_RESULT_OK) return status;
    if (!body->rotation_radians) {
        if (motion.magnitude) {
            result->rotation_axis = motion.axis;
            result->rotation_radians = motion.magnitude;
        }
        return NT_RESULT_OK;
    }
    StartingRotation initial;
    starting_rotation(body,&initial);
    if (compose_parallel_rotation(&initial,n,unit,motion,result)) return NT_RESULT_OK;
    if (fabs(initial.angle)<BODY_TINY_COMPOSITION && motion.magnitude<BODY_TINY_COMPOSITION)
        return compose_tiny_rotation(n,unit,&initial,result);
    return compose_body_rotation(&initial,n,unit,motion,result);
}

static NT_SIMULATION_NOINLINE NtResult integrate_angular(
    const NtFloatingBody *body, NtVec3 torque, double elapsed,
    NtSpatialExact delta[3], NtSpatialExact *delta_unit, NtVec3 *velocity)
{
    const double start[3] = {body->angular_velocity.x,body->angular_velocity.y,body->angular_velocity.z};
    NtSpatialExact initial_turn[3], limit;
    for (unsigned i=0;i<3;++i) nt_simulation_product(&initial_turn[i],start[i],elapsed);
    nt_spatial_exact_number(&limit,BODY_TURN_LIMIT*1000000,0);
    if (!nt_simulation_norm_within(initial_turn,&limit)) return NT_RESULT_OUT_OF_RANGE;
    NtSpatialExact acceleration[3], unit;
    const NtResult status = angular_acceleration(body,torque,acceleration,&unit);
    if (status!=NT_RESULT_OK) return status;

    NtSpatialExact micros, twice_micros, time, velocity_unit;
    nt_spatial_exact_number(&micros,1000000,0);
    nt_spatial_exact_number(&twice_micros,2000000,0);
    nt_spatial_exact_number(&time,elapsed,0);
    nt_spatial_exact_multiply(&velocity_unit,&unit,&micros);
    nt_spatial_exact_multiply(delta_unit,&velocity_unit,&twice_micros);
    NtSpatialExact final_turn[3], scaled_limit;
    double end[3];
    for (unsigned i=0;i<3;++i) {
        NtSpatialExact speed, before, impulse, after, sum;
        nt_spatial_exact_number(&speed,start[i],0);
        nt_spatial_exact_multiply(&before,&speed,&velocity_unit);
        nt_spatial_exact_multiply(&impulse,&acceleration[i],&time);
        nt_spatial_exact_add(&after,&before,&impulse);
        if (!bounded_quotient(&after,&velocity_unit,&end[i])) return NT_RESULT_OUT_OF_RANGE;
        nt_spatial_exact_multiply(&final_turn[i],&after,&time);
        nt_spatial_exact_add(&sum,&before,&after);
        nt_spatial_exact_multiply(&delta[i],&sum,&time);
    }
    nt_spatial_exact_multiply(&scaled_limit,&limit,&velocity_unit);
    if (!nt_simulation_norm_within(final_turn,&scaled_limit)) return NT_RESULT_OUT_OF_RANGE;
    *velocity = (NtVec3){end[0],end[1],end[2]};
    return NT_RESULT_OK;
}

static NtResult step_angular(const NtFloatingBody *body, NtVec3 torque, double elapsed, NtFloatingBody *result)
{
    NtSpatialExact delta[3], delta_unit;
    const NtResult status = integrate_angular(body,torque,elapsed,delta,&delta_unit,&result->angular_velocity);
    if (status!=NT_RESULT_OK) return status;
    return rotate_body(body,delta,&delta_unit,result);
}

NtResult nt_floating_body_step(const NtFloatingBody *body, const NtBodyLoad *load,
                                NtTimeUs elapsed, NtFloatingBody *out)
{
    if (!body || !load || !out) return NT_RESULT_INVALID_ARGUMENT;
    if (elapsed>NT_SIMULATION_STEP_LIMIT) return NT_RESULT_OUT_OF_RANGE;
    fenv_t saved;
    if (!nt_begin_standard_fp(&saved)) return NT_RESULT_UNSUPPORTED_CAPABILITY;
    if (!nt_simulation_valid_body(body) || !valid_load(load->force) || !valid_load(load->torque))
        return nt_simulation_finish(&saved,NT_RESULT_INVALID_ARGUMENT);

    NtFloatingBody result = *body;
    NtResult status = NT_RESULT_OK;
    if (elapsed) {
        const double time = (double)elapsed;
        if (!move_body(body,load->force,time,&result)) status = NT_RESULT_OUT_OF_RANGE;
        else status = step_angular(body,load->torque,time,&result);
    }
    status = nt_simulation_finish(&saved,status);
    if (status==NT_RESULT_OK) *out = result;
    return status;
}
