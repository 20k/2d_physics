#ifndef CHARACTER_HPP_INCLUDED
#define CHARACTER_HPP_INCLUDED

//#include "state.hpp"

struct physics_object_base;

struct does_chemical_reactions
{
    virtual void interact(float dt_s, chemical_interaction_base<physics_object_base>& items, state& st)
    {

    }
};

struct projectile;

///do damage properly with pending damage in damageable
struct physics_object_base : virtual moveable, virtual renderable, virtual collideable, virtual base_class, virtual network_serialisable, virtual does_chemical_reactions
{
    physics_object_base(int team) : collideable(team, collide::RAD)
    {
        collision_dim = {tex.getSize().x, tex.getSize().y};
    }

    virtual void tick(float dt_s, state& st) {};

    virtual void resolve_barrier_collisions(float dt_s, state& st) {};

    virtual void set_owner(int id)
    {
        network_serialisable::set_owner(id);

        team = id;
    }

    virtual void render(sf::RenderWindow& win, vec2f pos)
    {
        if(!should_render)
            return;

        if(out_of_bounds(win, pos))
            return;

        sf::Sprite spr(tex);
        spr.setOrigin(tex.getSize().x/2, tex.getSize().y/2);
        spr.setPosition({pos.x(), pos.y()});
        spr.setColor(sf::Color(255 * col.x(), 255 * col.y(),255 * col.z()));

        win.draw(spr);
    }
};

///slave network character
struct physics_object_client : virtual physics_object_base, virtual networkable_client
{
    bool have_pos = false;

    physics_object_client() : collideable(-1, collide::RAD), physics_object_base(-1) {}

    virtual byte_vector serialise_network() override
    {
        byte_vector vec;
        vec.push_back(pos);

        return vec;
    }

    virtual void deserialise_network(byte_fetch& fetch) override
    {
        vec2f fpos = fetch.get<vec2f>();

        pos = fpos;

        if(!have_pos)
        {
            collideable::init_collision_pos(pos);

            have_pos = true;
        }

        set_collision_pos(pos);
    }

    void render(sf::RenderWindow& win) override
    {
        renderable::render(win, pos, rotation);
    }
};

struct particle_parameters
{
    float bonding_keep_distance = 10.f;
    float hard_knock_distance = 30.f;
    float bond_strength = 0.45;

    ///fluid thickness > 0.3 = very pastey
    float fluid_thickness = 0.01f;
    float general_repulsion_mult = 2.5f;

    float particle_size = 1.f;
    float particle_mass = 1.f;
};

///solids next
struct physics_object_host : virtual physics_object_base, virtual networkable_host
{
    bool fixed = false;

    vec2f last_pos;
    vec2f leftover_pos_adjustment;

    vec2f try_next;

    vec2f player_acceleration;
    vec2f acceleration;
    vec2f impulse;

    bool stuck_to_surface = false;
    //bool jumped = false;
    float jump_stick_cooldown_cur = 0.f;
    float jump_stick_cooldown_time = 0.05f;

    float last_dt = 1.f;

    bool has_friction = true;

    int num_interacting = 0;
    float interaction_distance = 0.f;

    int num_bonds = 3;
    float bond_length = 40.f;

    bool is_solid = true;
    bool is_gas = false;

    float rotation_accumulate = 0.f;

    vec2f get_bond_dir_absolute(int num)
    {
        vec2f dir;

        if(num >= num_bonds)
        {
            printf("wtf are you doing, invalid bond num %i\n", num);
            return dir;
        }

        float bond_frac = (float)num / num_bonds;
        float bond_angle = bond_frac * 2 * M_PI;

        vec2f bond_dir = {cos(bond_angle), sin(bond_angle)};

        ///we're getting the absolute bond angle
        bond_dir = bond_dir.rot(rotation);

        return bond_dir;
    }

    physics_object_host(int team, network_state& ns) : physics_object_base(team), collideable(team, collide::RAD), networkable_host(ns)
    {
        rotation = randf_s(0.f, M_PI);
    }

    void render(sf::RenderWindow& win) override
    {
        renderable::render(win, pos, rotation);

        for(int i = 0; i < num_bonds; i++)
        {
            vec2f abs_dir = get_bond_dir_absolute(i);

            sf::RectangleShape shape;
            shape.setSize({bond_length, 2});
            shape.setOrigin(0, 1);

            shape.setPosition(pos.x(), pos.y());
            shape.setRotation(r2d(abs_dir.angle()));

            win.draw(shape);
        }
    }

    vec2f reflect_physics(vec2f next_pos, physics_barrier* bar)
    {
        vec2f cdir = (next_pos - pos).norm();
        float clen = (next_pos - pos).length();

        //vec2f ndir = (bar->p2 - bar->p1).norm();

        vec2f ndir = reflect((next_pos - pos).norm(), bar->get_normal());

        vec2f to_line = point2line_shortest(bar->p1, (bar->p2 - bar->p1).norm(), pos);

        pos += to_line - to_line.norm();
        next_pos = ndir * clen + pos;// + to_line - to_line.norm() * 0.25f;

        return next_pos;
    }

    float speed_modulate(float in_speed, float angle) const
    {
        //printf("%f angle\n", r2d(angle));

        angle = fabs(angle);

        ///arbitrary
        float frictionless_angle = d2r(50.f);

        if(angle < frictionless_angle)
            return in_speed;

        //printf("%f %f a\n", angle, frictionless_angle);

        float slow_frac = (angle - frictionless_angle) / ((M_PI/2.f) - frictionless_angle);

        slow_frac = clamp(slow_frac, 0.f, 1.f);

        slow_frac = pow(slow_frac, 2.f);

        slow_frac /= 2.f;

        //printf("%f slow\n", slow_frac);

        return in_speed * (1.f - slow_frac);
    }

    ///instead of line normal use vertical
    ///aha: Fundamental issue, to_line can be wrong direction because we hit the vector from underneath
    ///stick physics still the issue
    ///it seems likely that we have a bad accum, which then causes the character to be pushed through a vector
    ///test if placing a point next to the new vector rwith the relative position of the old, then applying the accum * extreme
    ///would push the character through the vector. If true, rverse it
    vec2f stick_physics(vec2f next_pos, physics_barrier* bar, physics_barrier* closest, vec2f& accumulate_shift) const
    {
        vec2f cdir = (next_pos - pos).norm();
        float clen = (next_pos - pos).length();

        vec2f to_line = point2line_shortest(bar->p1, (bar->p2 - bar->p1).norm(), pos);

        float a1 = angle_between_vectors((bar->p2 - bar->p1).norm(), cdir);
        float a2 = angle_between_vectors((bar->p1 - bar->p2).norm(), cdir);

        vec2f dir;

        if(fabs(a1) < fabs(a2))
        {
            dir = (bar->p2 - bar->p1).norm();
        }
        else
        {
            dir = (bar->p1 - bar->p2).norm();
        }

        //vec2f projected = projection(next_pos - pos, dir);
        vec2f projected = dir.norm() * speed_modulate(clen, angle_between_vectors(dir, next_pos - pos));

        //vec2f projected = dir.norm() * clen;

        accumulate_shift += -to_line.norm();

        next_pos = projected + pos;

        return next_pos;
    }

    physics_barrier* get_closest(vec2f next_pos, physics_barrier_manager& physics_barrier_manage)
    {
        float min_dist = FLT_MAX;
        physics_barrier* min_bar = nullptr;

        for(physics_barrier* bar : physics_barrier_manage.objs)
        {
            vec2f dist_intersect = point2line_intersection(pos, next_pos, bar->p1, bar->p2) - pos;

            //vec2f to_line_base = point2line_shortest(closest->p1, (closest->p2 - closest->p1).norm(), pos);

            ///might not work 100% for very shallow non convex angles
            if(dist_intersect.length() < min_dist && bar->crosses(pos, next_pos))
            {
                min_dist = dist_intersect.length();
                min_bar = bar;
            }
        }

        return min_bar;
    }


    bool crosses_with_normal(vec2f p1, vec2f next_pos, physics_barrier* bar)
    {
        if(has_default)
            return bar->crosses(p1, next_pos) && bar->on_normal_side_with_default(p1, on_default_side);// && bar->on_normal_side(p1);
        else
        {
            return bar->crosses(p1, next_pos);
        }

    }

    bool any_crosses_with_normal(vec2f p1, vec2f next_pos, physics_barrier_manager& physics_barrier_manage)
    {
        for(physics_barrier* bar : physics_barrier_manage.objs)
        {
            if(crosses_with_normal(p1, next_pos, bar))
                return true;
        }

        return false;
    }

    bool full_test(vec2f pos, vec2f next_pos, vec2f accum, physics_barrier_manager& physics_barrier_manage)
    {
        return !any_crosses_with_normal(next_pos, next_pos + accum, physics_barrier_manage) && !any_crosses_with_normal(pos, pos + accum, physics_barrier_manage) && !any_crosses_with_normal(pos + accum, next_pos + accum, physics_barrier_manage);
    }

    #if 1

    ///if the physics still refuses to work:
    ///do normal physics but ignore accum
    ///then for each accum term, test to see if this causes intersections
    ///if it does, flip it

    ///Ok: last fix now
    ///We can avoid having to define normal systems by using the fact that normals are consistent
    ///If we have double collisions, we can probably use the normal of my current body i'm intersecting with/near and then
    ///use that to define the appropriate normal of the next body (ie we can check if we hit underneath)
    ///this should mean that given consistently defined normals (ie dont randomly flip adjacent), we should be fine
    vec2f adjust_next_pos_for_physics(vec2f next_pos, physics_barrier_manager& physics_barrier_manage)
    {
        physics_barrier* min_bar = get_closest(next_pos, physics_barrier_manage);

        if(side_time > side_time_max)
            has_default = false;

        if(min_bar && !has_default)
        {
            has_default = true;
            on_default_side = min_bar->on_normal_side(pos);
        }

        vec2f accum;

        vec2f move_dir = {0,0};
        vec2f large_rad_dir = {0,0};

        bool any_large_rad = false;

        vec2f original_next = next_pos;

        stuck_to_surface = false;

        for(physics_barrier* bar : physics_barrier_manage.objs)
        {
            if(crosses_with_normal(pos, next_pos, bar))
            {
                //next_pos = stick_physics(next_pos, bar, min_bar, accum);
                next_pos = reflect_physics(next_pos, bar);
            }

            float line_jump_dist = 2;

            vec2f dist_perp = point2line_shortest(bar->p1, (bar->p2 - bar->p1).norm(), pos);

            if(dist_perp.length() < line_jump_dist && bar->within(next_pos))
            {
                stuck_to_surface = true;

                side_time = 0;
            }
        }

        accum = {0,0};

        for(physics_barrier* bar : physics_barrier_manage.objs)
        {
            if(!bar->crosses(pos, original_next))
                continue;

            vec2f to_line = point2line_shortest(bar->p1, (bar->p2 - bar->p1).norm(), next_pos);

            float line_distance = 2.f;

            float dir = 0.f;

            if(!any_crosses_with_normal(next_pos, next_pos - to_line.norm() * 5, physics_barrier_manage))
            {
                dir = -1;
            }
            else
            {
                if(!any_crosses_with_normal(next_pos, next_pos + to_line.norm() * 5, physics_barrier_manage))
                {
                    dir = 1;
                }
            }

            float dist = line_distance - to_line.length();

            float extra = 1.f;

            if(dist > 0)
            {
                accum += dir * to_line.norm() * dist;
            }
        }

        if(accum.sum_absolute() > 0.00001f)
            accum = accum.norm();

        if(full_test(pos, next_pos, accum, physics_barrier_manage))
        {
            pos += accum;
            next_pos += accum;
        }
        else
        {
            if(full_test(pos, next_pos, -accum, physics_barrier_manage))
            {
                pos += -accum;
                next_pos += -accum;
            }
            else
            {
                //printf("oops 2\n");
            }

            //printf("oops\n");
        }

        if(any_crosses_with_normal(pos, next_pos, physics_barrier_manage))
        {
            next_pos = pos;
        }

        return next_pos;
    }
    #endif

    /*vec2f try_reflect_physics(vec2f next_pos, physics_barrier_manager& physics_barrier_manage)
    {
        for(physics_barrier* bar : physics_barrier_manage.phys)
        {
            if(bar->crosses(pos, next_pos))
            {
                next_pos = reflect_physics(next_pos, bar);
            }
        }

        return next_pos;
    }*/

    void spawn(vec2f spawn_pos, game_world_manager& game_world_manage)
    {
        pos = spawn_pos;
        last_pos = pos;

        should_render = true;
    }

    particle_parameters params;

    virtual void interact(float dt_s, chemical_interaction_base<physics_object_base>& items, state& st) override
    {
        if(fixed)
            return;

        vec2f next_pos = try_next;

        ///relax later
        vec2f accum = {0, 0};

        int num = 0;

        int relax_count = 2;

        for(int kk=0; kk<relax_count; kk++)
        for(int i=0; i<items.objs.size(); i++)
        {
            physics_object_base* obj = items.objs[i];

            if((void*)this == (void*)obj)
                continue;

            vec2f their_pos = obj->pos;

            vec2f to_them = their_pos - pos;

            float tlen = to_them.length();

            if(tlen > 200)
                continue;

            physics_object_host* real = dynamic_cast<physics_object_host*>(obj);

            if(!real)
                continue;

            //bool ignore = false;

            vec2f saved_next = next_pos;

            ///maybe allow solids to trap a layer of liquids for fun?
            ///is solid will later be a derived property
            ///need rotation next, ie bond stiffness
            ///also want to rotate uninterfacing molecules so that they try and bond perhaps?
            if(is_solid && real->is_solid && tlen > params.hard_knock_distance)
            {
                for(int my_bond_c = 0; my_bond_c < num_bonds; my_bond_c++)
                {
                    vec2f my_bond_dir = get_bond_dir_absolute(my_bond_c);

                    vec2f my_bond_pos = my_bond_dir * bond_length + pos;

                    for(int their_bond_c = 0; their_bond_c < real->num_bonds; their_bond_c++)
                    {
                        vec2f their_bond_dir = real->get_bond_dir_absolute(their_bond_c);

                        vec2f their_bond_pos = their_bond_dir * real->bond_length + real->pos;

                        vec2f my_to_them = (their_bond_pos - my_bond_pos);

                        float inter_bond_distance = my_to_them.length();

                        if(inter_bond_distance < params.bonding_keep_distance)
                        {
                            vec2f base_accel = my_to_them;

                            float unsigned_angle = fabs(angle_between_vectors(my_bond_dir, -their_bond_dir));

                            float max_bond_angle = d2r(30.f);

                            float angle_frac = unsigned_angle / max_bond_angle;

                            if(angle_frac > 1)
                                angle_frac = 1;

                            float unsigned_angle_frac = 1.f - angle_frac;

                            float force_mult = params.bond_strength;

                            base_accel = base_accel * force_mult / relax_count;

                            //accum += base_accel / relax_count;

                            ///this is causing the oscillation, because we accelerate when shifting next_pos
                            next_pos = next_pos + base_accel * unsigned_angle_frac;

                            float sangle = signed_angle_between_vectors(my_bond_dir, -their_bond_dir) / 50.f;

                            //printf("sangle %f\n", sangle);
                            //printf("real_angle %f\n", angle_between_vectors(my_bond_dir, their_bond_dir));

                            float abound = M_PI/5.f;

                            //sangle = clamp(sangle, -abound, abound);

                            //if(fabs(sangle) > M_PI/1000.f)
                                rotation_accumulate += (sangle) / relax_count;

                            //printf("%f\n", signed_angle_between_vectors(my_bond_dir, their_bond_dir));

                            //ignore = true;

                            /*sf::CircleShape shape;
                            shape.setRadius(6);
                            shape.setOrigin(3, 3);

                            shape.setPosition(my_bond_pos.x(), my_bond_pos.y());

                            st.debug_window.draw(shape);*/
                        }
                    }
                }
            }

            vec2f diff = (next_pos - saved_next);

            leftover_pos_adjustment += diff/1.1f;

            float rdist = 40.f;

            /*if(to_them.length() >= rdist)
            {
                continue;
            }*/

            /*float extra = rdist - to_them.length();

            next_pos = their_pos - to_them.norm() * rdist;*/


            vec2f nto_them = (to_them / tlen);

            float approx_vel = (try_next - pos).length();
            float their_approx = ((real->pos - real->last_pos)).length();
            //float their_approx = ((real->pos - real->last_pos) + (vec2f){0, 1} * GRAVITY_STRENGTH * ((dt_s + last_dt)/2.f) * dt_s * FORCE_MULTIPLIER).length();

            if(tlen < rdist * 4 && !is_gas)
            {
                num_interacting++;

                interaction_distance += 1.f - (tlen / (rdist * 4));

                float tdist = 1.f - (tlen / (rdist * 4));

                //next_pos = (next_pos - pos).norm() * mix((next_pos - pos).length(), their_approx, 0.45f) + pos;

                ///this essentially controls the dial between liquid and gas
                ///below check is good for liquids
                //next_pos = (next_pos - pos).norm() * mix((next_pos - pos).length(), their_approx, 0.1f * tdist / relax_count) + pos;

                ///this check seems to be more successful than the above
                //next_pos = mix((next_pos - pos), (real->pos - real->last_pos), 0.01f * tdist / relax_count) + pos;
                next_pos = mix((next_pos - pos), (real->try_next - real->pos), params.fluid_thickness * tdist / relax_count) + pos;

                /*if(real->fixed)
                {
                    printf("%f\n", (real->try_next - real->pos).length());
                }*/

                //next_pos = mix(next_pos - pos, (real->try_next - real->pos), 0.45f) + pos;
           }

           if(tlen < params.hard_knock_distance)
           {
                float extra = params.hard_knock_distance - tlen;

                next_pos = next_pos - to_them.norm() * extra * 2.f / relax_count;
           }

           ///do solids here
           ///model valence shells and have this affect bonding angles
           ///ie i have no idea what i'm doing google bond angles

            #if 0

            if(tlen < rdist)
            {
                float extra = rdist - tlen;

                //next_pos = their_pos - to_them.norm() * rdist;

                next_pos = next_pos - nto_them * extra * 0.5f / relax_count;

                //acceleration += -to_them * GRAVITY_STRENGTH / 5.f;

                //acceleration += -to_them.norm() * extra * 1000.f * 1.f;

                //continue;
            }

            if(tlen < rdist * 2.f)
            {
                float target_hold = rdist * 1.5f;

                if(tlen > target_hold)
                {
                    float extra = tlen - target_hold;

                    accum += nto_them * extra * 0.001f / relax_count;

                    //next_pos = next_pos + nto_them * extra / 5.f;
                }
                /*if(tlen < target_hold - 2.f)
                {
                    float extra = target_hold - tlen;

                    accum += -nto_them * extra * 0.01f / relax_count;

                    //next_pos = next_pos - nto_them * extra / 5.f;
                }*/
            }
            #endif

            //if(ignore)
            //    continue;

            if(to_them.length() < 0.1)
                continue;

            float force_mult = params.general_repulsion_mult;

            if(is_solid)
                force_mult = params.general_repulsion_mult * 2;

            float force = (1.f/(tlen * tlen)) * force_mult;

            if(force > 10)
                force = 10;

            //next_pos = next_pos - to_them.norm() * force;

            accum += -nto_them * force / relax_count;

            /*vec2f test_next = pos - to_them.norm() * extra / 2.f;

            accum += test_next - pos;

            //num++;
            num = 1;*/
        }

        //leftover_pos_adjustment = (next_pos - try_next);

        /*if((next_pos - pos).length() > 10.f)
        {
            next_pos = (next_pos - pos).norm() * 10.f + pos;
        }*/

        //if(num > 0)
        //    accum = accum / (float)num;

        /*float max_repulse = 1.f;

        if(accum.length() > max_repulse)
        {
            accum = accum.norm() * max_repulse;
        }*/

        //try_next = (next_pos - pos).length() * (next_pos + accum - pos).norm() + pos;

        try_next = next_pos;

        acceleration += accum * 1000.f * 1000.f;

        //rotation += rotation_accumulate;

        //try_next = next_pos;
    }

    void tick(float dt, state& st) override
    {
        do_gravity({0, 1});

        float dt_f = dt / last_dt;

        vec2f friction = {1.f, 1.f};

        friction = {0.999, 0.999};

        if(stuck_to_surface)
        {
            friction = {0.98f, 0.98f};
        }

        /*if(num_interacting > 0)
        {
            friction = friction - (interaction_distance) * 0.3f;

            friction = clamp(friction, 0.5f, 1.f);
        }*/

        num_interacting = 0;
        interaction_distance = 0.f;

        //printf("%f\n", xv);

        //vec2f next_pos_player_only = pos + (pos - last_pos) * dt_f * friction + player_acceleration * dt * dt;

        //vec2f next_pos = pos + (pos - last_pos) * dt_f * friction + acceleration * ((dt + last_dt)/2.f) * dt + impulse;
        ///not sure if we need to factor in (dt + last_dt)/2 into impulse?
        vec2f next_pos = pos + (pos - last_pos) * dt_f * friction + acceleration * ((dt + last_dt)/2.f) * dt * FORCE_MULTIPLIER + (impulse * dt) * FORCE_MULTIPLIER;

        float max_speed = 0.85f * FORCE_MULTIPLIER;

        next_pos += player_acceleration * dt * dt;

        try_next = next_pos;

        if(fixed)
        {
            try_next = pos;
        }

        player_acceleration = {0,0};
        acceleration = {0,0};
        impulse = {0,0};
    }

    virtual void resolve_barrier_collisions(float dt, state& st) override
    {
        vec2f next_pos = adjust_next_pos_for_physics(try_next, st.physics_barrier_manage);

        last_dt = dt;

        if((next_pos - pos).length() > 10.f)
        {
            vec2f diff = -next_pos + ((next_pos - pos).norm() * 10.f + pos);

            next_pos += diff;
            //pos += diff;
        }

        pos += leftover_pos_adjustment;
        leftover_pos_adjustment = {0,0};

        if(fixed)
        {
            next_pos = pos;
        }

        last_pos = pos;
        pos = next_pos;

        set_collision_pos(pos);

        side_time += dt;

        rotation += rotation_accumulate;
        rotation_accumulate = 0.f;
    }

    void do_gravity(vec2f dir)
    {
        acceleration += dir * GRAVITY_STRENGTH;
    }

    void set_movement(vec2f dir)
    {
        player_acceleration += dir * 8.f;
    }

    byte_vector serialise()
    {
        byte_vector ret;

        ret.push_back<vec2f>(pos);

        return ret;
    }

    void deserialise(byte_fetch& fetch)
    {
        pos = fetch.get<vec2f>();
    }

    virtual byte_vector serialise_network() override
    {
        byte_vector vec;

        vec.push_back<vec2f>(pos);


        return vec;
    }

    virtual void deserialise_network(byte_fetch& fetch) override
    {
        vec2f fpos = fetch.get<vec2f>();
    }
};

struct physics_object_manager : virtual renderable_manager_base<physics_object_base>, virtual collideable_manager_base<physics_object_base>, virtual network_manager_base<physics_object_base>, virtual chemical_interaction_base<physics_object_base>
{
    timestep_state fts1;
    timestep_state fts2;

    void tick(float dt, state& st)
    {
        int nsteps = fts1.step(dt);

        //printf("steps %i\n", nsteps);

        //printf("ftime %f\n", dt * 1000.f);

        for(int kk=0; kk < nsteps; kk++)
        {
            for(physics_object_base* c : objs)
            {
                c->tick(fts1.get_max_step(dt), st);
            }
        }
    }

    void resolve_barrier_collisions(float dt, state& st)
    {
        int nsteps = fts2.step(dt);

        for(int kk=0; kk < nsteps; kk++)
        {
            for(physics_object_base* c : objs)
            {
                c->resolve_barrier_collisions(fts2.get_max_step(dt), st);
            }
        }
    }

    virtual ~physics_object_manager(){}
};

#endif // CHARACTER_HPP_INCLUDED
