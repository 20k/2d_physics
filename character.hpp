#ifndef CHARACTER_HPP_INCLUDED
#define CHARACTER_HPP_INCLUDED

//#include "state.hpp"

struct physics_object_base;

struct does_chemical_reactions
{
    virtual void interact(float dt_s, chemical_interaction_base<physics_object_base>& items)
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
        renderable::render(win, pos);
    }
};

struct physics_object_host : virtual physics_object_base, virtual networkable_host
{
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

    physics_object_host(int team, network_state& ns) : physics_object_base(team), collideable(team, collide::RAD), networkable_host(ns)
    {

    }

    void render(sf::RenderWindow& win) override
    {
        renderable::render(win, pos);
    }

    /*vec2f reflect_physics(vec2f next_pos, physics_barrier* bar)
    {
        vec2f cdir = (next_pos - pos).norm();
        float clen = (next_pos - pos).length();

        //vec2f ndir = (bar->p2 - bar->p1).norm();

        vec2f ndir = reflect((next_pos - pos).norm(), bar->get_normal());

        vec2f to_line = point2line_shortest(bar->p1, (bar->p2 - bar->p1).norm(), pos);

        pos += to_line - to_line.norm();
        next_pos = ndir * clen + pos;// + to_line - to_line.norm() * 0.25f;

        return next_pos;
    }*/

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
                next_pos = stick_physics(next_pos, bar, min_bar, accum);
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

    virtual void interact(float dt_s, chemical_interaction_base<physics_object_base>& items) override
    {
        vec2f next_pos = try_next;

        ///relax later

        vec2f accum = {0, 0};

        int num = 0;

        int relax_count = 10;

        for(int kk=0; kk<relax_count; kk++)
        for(int i=0; i<items.objs.size(); i++)
        {
            physics_object_base* obj = items.objs[i];

            if((void*)this == (void*)obj)
                continue;

            float rdist = 15.f;

            vec2f their_pos = obj->pos;

            vec2f to_them = their_pos - next_pos;

            /*if(to_them.length() >= rdist)
            {
                continue;
            }*/

            /*float extra = rdist - to_them.length();

            next_pos = their_pos - to_them.norm() * rdist;*/

            //if(to_them.length() < 0.1)
            //    continue;

            if(to_them.length() < rdist)
            {
                float extra = rdist - to_them.length();

                //next_pos = their_pos - to_them.norm() * rdist;

                next_pos = next_pos - to_them.norm() * extra / relax_count;

                //acceleration += -to_them * GRAVITY_STRENGTH / 5.f;

                //acceleration += -to_them.norm() * extra * 1000.f * 1.f;

                //continue;
            }

            float force = (1.f/(to_them.length() * to_them.length())) * 0.55f;

            //if(force > 10)
            //    force = 10;

            //next_pos = next_pos - to_them.norm() * force;

            accum += -to_them.norm() * force / relax_count;

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

        //try_next = next_pos;
    }

    void tick(float dt, state& st) override
    {
        do_gravity({0, 1});

        float dt_f = dt / last_dt;

        vec2f friction = {1.f, 1.f};

        if(stuck_to_surface)
        {
            friction = {0.5f, 0.5};
        }

        //printf("%f\n", xv);

        //vec2f next_pos_player_only = pos + (pos - last_pos) * dt_f * friction + player_acceleration * dt * dt;

        //vec2f next_pos = pos + (pos - last_pos) * dt_f * friction + acceleration * ((dt + last_dt)/2.f) * dt + impulse;
        ///not sure if we need to factor in (dt + last_dt)/2 into impulse?
        vec2f next_pos = pos + (pos - last_pos) * dt_f * friction + acceleration * ((dt + last_dt)/2.f) * dt * FORCE_MULTIPLIER + (impulse * dt) * FORCE_MULTIPLIER;

        float max_speed = 0.85f * FORCE_MULTIPLIER;

        next_pos += player_acceleration * dt * dt;

        try_next = next_pos;

        player_acceleration = {0,0};
        acceleration = {0,0};
        impulse = {0,0};
    }

    virtual void resolve_barrier_collisions(float dt, state& st) override
    {
        vec2f next_pos = adjust_next_pos_for_physics(try_next, st.physics_barrier_manage);

        last_dt = dt;

        if((next_pos - pos).length() > 1000.f * 1000.f * dt)
        {
            vec2f diff = -next_pos + ((next_pos - pos).norm() * 1000.f * 1000.f * dt + pos);

            next_pos += diff;
            //pos += diff;
        }

        pos += leftover_pos_adjustment;
        leftover_pos_adjustment = {0,0};

        last_pos = pos;
        pos = next_pos;

        set_collision_pos(pos);

        side_time += dt;
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
