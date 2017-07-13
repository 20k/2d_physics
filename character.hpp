#ifndef CHARACTER_HPP_INCLUDED
#define CHARACTER_HPP_INCLUDED

//#include "state.hpp"

struct projectile;

///do damage properly with pending damage in damageable
struct character_base : virtual moveable, virtual renderable, virtual damageable_base, virtual collideable, virtual base_class, virtual network_serialisable
{
    character_base(int team) : collideable(team, collide::RAD)
    {
        collision_dim = {tex.getSize().x, tex.getSize().y};
    }

    virtual void tick(float dt_s, state& st) {};

    virtual void set_owner(int id)
    {
        network_serialisable::set_owner(id);

        team = id;
    }

    virtual bool can_collide() override
    {
        return hp > 0.f;
    }
};

///slave network character
struct character : virtual character_base, virtual networkable_client, virtual damageable_client
{
    bool have_pos = false;

    character() : collideable(-1, collide::RAD), character_base(-1) {}

    virtual byte_vector serialise_network() override
    {
        byte_vector vec;
        vec.push_back(pos);

        vec.push_vector(damageable_client::serialise_network());

        //vec.push_back(damageable_client)

        return vec;
    }

    virtual void deserialise_network(byte_fetch& fetch) override
    {
        vec2f fpos = fetch.get<vec2f>();

        pos = fpos;

        damageable_client::deserialise_network(fetch);

        if(!have_pos)
        {
            collideable::init_collision_pos(pos);

            have_pos = true;
        }

        set_collision_pos(pos);
    }

    void render(sf::RenderWindow& win) override
    {
        //if(!spawned)
        //    return;

        if(hp < 0)
            return;

        renderable::render(win, pos);
    }
};

///this is a player character
struct player_character : virtual character_base, virtual networkable_host, virtual damageable_host
{
    bool spawned = false;
    float spawn_timer = 0.f;
    float spawn_timer_max = 5.f;

    vec2f last_pos;

    //vec2f velocity;
    vec2f player_acceleration;
    vec2f acceleration;
    vec2f impulse;


    bool stuck_to_surface = false;
    //bool jumped = false;
    float jump_stick_cooldown_cur = 0.f;
    float jump_stick_cooldown_time = 0.05f;

    bool can_jump = false;
    vec2f jump_dir;

    float last_dt = 1.f;

    float jump_cooldown_cur = 0.f;
    float jump_cooldown_time = 0.15f;

    player_character(int team, network_state& ns) : character_base(team), collideable(team, collide::RAD), networkable_host(ns), damageable_host(ns)
    {

    }

    /*void on_collide(collideable* other)
    {
        if(dynamic_cast<projectile*>(other) != nullptr)
        {
            damage(0.6f);
        }
    }*/

    void render(sf::RenderWindow& win) override
    {
        //if(!spawned)
        //    return;

        renderable::render(win, pos);
    }

    vec2f last_resort_physics(vec2f next_pos, physics_barrier* bar)
    {
        vec2f total_dir = next_pos - pos;

        for(int i=0; i<100; i++)
        {
            if(bar->crosses(pos, pos + total_dir))
            {
                total_dir = total_dir / 2.f;
            }
            else
            {
                return pos + total_dir;
            }
        }

        return pos;
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

    /*float speed_modulate(float in_speed, float angle)
    {
        if(angle >= M_PI)
            return 0.f;

        ///1 -> 0, 1 at min angle and 0 at max
        float iv_angle_frac = 1.f - (angle / M_PI);

        float new_speed = in_speed * iv_angle_frac;

        float weighting_towards_new = 0.6f;

        return mix(in_speed, new_speed, weighting_towards_new);
    }*/

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
        //vec2f projected = dir.norm() * speed_modulate(clen, angle_between_vectors(dir, next_pos - pos));

        vec2f projected = dir.norm() * clen;

        /*if(closest != nullptr)
        {
            vec2f to_line_base = point2line_shortest(closest->p1, (closest->p2 - closest->p1).norm(), pos);

            float angle = angle_between_vectors(to_line, to_line_base);

            if(angle >= M_PI/2.f)
            {
                to_line = -to_line;
            }
        }*/

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

    ///if the physics still refuses to work:
    ///do normal physics but ignore accum
    ///then for each accum term, test to see if this causes intersections
    ///if it does, flip it
    vec2f adjust_next_pos_for_physics(vec2f next_pos, physics_barrier_manager& physics_barrier_manage)
    {
        physics_barrier* min_bar = get_closest(next_pos, physics_barrier_manage);

        vec2f accum;

        vec2f move_dir = {0,0};
        vec2f large_rad_dir = {0,0};

        bool any_large_rad = false;

        vec2f original_next = next_pos;

        for(physics_barrier* bar : physics_barrier_manage.objs)
        {
            if(bar->crosses(pos, next_pos))
            {
                next_pos = stick_physics(next_pos, bar, min_bar, accum);
            }

            float line_jump_dist = 2;

            vec2f dist_perp = point2line_shortest(bar->p1, (bar->p2 - bar->p1).norm(), pos);

            if(dist_perp.length() < line_jump_dist && bar->within(next_pos))
            {
                stuck_to_surface = true;
                can_jump = true;
                jump_dir += bar->get_normal_towards(next_pos);
            }
        }

        /*if(min_bar)
        {
            if(min_bar->crosses(pos, next_pos))
            {
                next_pos = stick_physics(next_pos, min_bar, min_bar, accum);
            }
        }*/

        accum = {0,0};

        for(physics_barrier* bar : physics_barrier_manage.objs)
        {
            if(!bar->crosses(pos, original_next))
                continue;

            vec2f to_line = point2line_shortest(bar->p1, (bar->p2 - bar->p1).norm(), next_pos);

            if(!physics_barrier_manage.any_crosses(next_pos, next_pos - to_line.norm() * 5))
            {
                accum += -to_line.norm();
            }
            else
            {
                if(!physics_barrier_manage.any_crosses(next_pos, next_pos + to_line.norm() * 5))
                {
                    accum += to_line.norm();
                }
            }
        }

        /*if(min_bar)
        {
            if(min_bar->crosses(pos, next_pos))
            {
                next_pos = stick_physics(next_pos, min_bar, min_bar, accum);
            }
        }*/

        if(physics_barrier_manage.any_crosses(pos, next_pos))
        {
            printf("clip\n");
        }

        bool failure_state = false;

        if(accum.sum_absolute() > 0.00001f)
            accum = accum.norm();

        //if(!physics_barrier_manage.any_crosses(pos, next_pos))
        {
            if(!physics_barrier_manage.any_crosses(next_pos, next_pos + accum) && !physics_barrier_manage.any_crosses(pos, pos + accum) && !physics_barrier_manage.any_crosses(pos + accum, next_pos + accum))
            {
                pos += accum;
                next_pos += accum;
            }
            else
            {
                failure_state = true;

                if(!physics_barrier_manage.any_crosses(next_pos, next_pos + -accum) && !physics_barrier_manage.any_crosses(pos, pos + -accum) && !physics_barrier_manage.any_crosses(pos + -accum, next_pos + -accum))
                {
                    pos += -accum;
                    next_pos += -accum;

                    failure_state = false;
                }
                else
                {
                    printf("oops 2\n");
                }

                printf("oops\n");
            }
        }
        /*else
        {
            failure_state = true;
        }*/


        /*if(failure_state)
        {
            int inum = 40;

            for(int i=0; i<inum; i++)
            {
                float frac = (float)i / inum;

                float angle = frac * 2 * M_PI;

                float x = cos(angle);
                float y = sin(angle);

                vec2f test = {x, y};

                if(!physics_barrier_manage.any_crosses(next_pos, next_pos + test) && !physics_barrier_manage.any_crosses(pos, pos + test) && !physics_barrier_manage.any_crosses(pos + test, next_pos + test))
                {
                    pos += test;
                    next_pos += test;

                    printf("recovered from failure state\n");

                    break;
                }
            }
        }*/

        if(physics_barrier_manage.any_crosses(pos, next_pos))
        {
            next_pos = pos;
        }

        return next_pos;
    }

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

    vec2f force_enforce_no_clipping(vec2f next_pos, physics_barrier_manager& physics_barrier_manage)
    {
        for(physics_barrier* bar : physics_barrier_manage.objs)
        {
            if(bar->crosses(pos, next_pos))
            {
                //next_pos = last_resort_physics(next_pos, bar);

                //printf("hello\n");

                next_pos = pos;
            }
        }

        return next_pos;
    }

    void spawn(game_world_manager& game_world_manage)
    {
        vec2f spawn_pos = game_world_manage.get_next_spawn();

        pos = spawn_pos;
        last_pos = pos;

        reset_hp();

        should_render = true;
        spawn_timer = 0;
    }

    void tick_spawn(float dt, game_world_manager& game_world_manage)
    {
        if(dead())
        {
            should_render = false;

            spawn_timer += dt;

            if(spawn_timer >= spawn_timer_max)
            {
                spawn(game_world_manage);
            }
        }
    }

    void tick(float dt, state& st) override
    {
        stuck_to_surface = false;
        can_jump = false;
        jump_dir = {0,0};

        //clamp_velocity();

        //if(dt <= 0.000001f)
        //    return;

        do_gravity({0, 1});

        float dt_f = dt / last_dt;

        //dt_f = 1.f;

        //float friction = 0.99f;

        vec2f friction = {1.f, 1.f};

        //vec2f next_pos_player_only = pos + (pos - last_pos) * dt_f * friction + player_acceleration * dt * dt;

        //vec2f next_pos = pos + (pos - last_pos) * dt_f * friction + acceleration * ((dt + last_dt)/2.f) * dt + impulse;
        ///not sure if we need to factor in (dt + last_dt)/2 into impulse?
        vec2f next_pos = pos + (pos - last_pos) * dt_f * friction + acceleration * ((dt + last_dt)/2.f) * dt + (impulse * dt);

        float max_speed = 0.85f;

        /*if((next_pos - pos).length() > max_speed)
        {
            next_pos = (next_pos - pos).norm() * max_speed + pos;
        }*/

        //next_pos = adjust_next_pos_for_physics(next_pos, physics_barrier_manage);

        /*if(fabs((next_pos_player_only - pos).x()) > max_speed)
        {
            float diff = (next_pos_player_only - pos).x();

            if(diff < 0)
                next_pos_player_only.x() = -max_speed + pos.x();
            else
                next_pos_player_only.x() = max_speed + pos.x();
        }*/

        /*if(fabs((next_pos - pos).x() + player_acceleration.x() * dt * dt) < max_speed)
        {
            next_pos += player_acceleration * dt * dt;
        }*/

        vec2f my_speed = {(next_pos - pos).x(), 0.f};
        vec2f my_acc = {player_acceleration.x() * dt * dt, 0.f};

        if(stuck_to_surface)
        {
            my_speed.y() = (next_pos - pos).y();
        }

        if(fabs((my_speed + my_acc).length()) < max_speed || (fabs((my_speed + my_acc).length()) < fabs(my_speed.length())))
        {
            next_pos += player_acceleration * dt * dt;
        }

        //vec2f next_pos = next_pos_player_only + acceleration * dt * dt + impulse;

        next_pos = adjust_next_pos_for_physics(next_pos, st.physics_barrier_manage);

        //next_pos = force_enforce_no_clipping(next_pos, physics_barrier_manage);
        //next_pos = force_enforce_no_clipping(next_pos, st.physics_barrier_manage);

        //pos = pos + velocity * dt;

        last_dt = dt;

        last_pos = pos;
        pos = next_pos;

        //std::cout << "npos " << pos << " cpos " << last_pos << std::endl;

        //std::cout << pos << " " << acceleration << std::endl;

        //std::cout << (pos - last_pos).length() << std::endl;

        player_acceleration = {0,0};
        acceleration = {0,0};
        impulse = {0,0};

        jump_cooldown_cur += dt;
        jump_stick_cooldown_cur += dt;

        tick_spawn(dt, st.game_world_manage);

        //last_collision_pos = collision_pos;
        //collision_pos = pos;
        set_collision_pos(pos);
    }

    void do_gravity(vec2f dir)
    {
        acceleration += dir * GRAVITY_STRENGTH;
    }

    void set_movement(vec2f dir)
    {
        player_acceleration += dir * 8.f;
    }

    void jump()
    {
        if(!can_jump)
            return;

        if(jump_cooldown_cur < jump_cooldown_time)
            return;

        impulse += jump_dir.norm() * 0.45f * 1000.f;
        //acceleration += jump_dir.norm() * 200000.f;
        jump_cooldown_cur = 0;
        jump_stick_cooldown_cur = 0.f;

        //std::cout << jump_dir << std::endl;
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

        vec.push_vector(damageable_host::serialise_network());

        return vec;
    }

    virtual void deserialise_network(byte_fetch& fetch) override
    {
        vec2f fpos = fetch.get<vec2f>();

        //pos = fpos;

        damageable_host::deserialise_network(fetch);
    }
};

/*struct character_manager
{
    std::vector<character*> characters;

    void add(character* c)
    {
        characters.push_back(c);
    }

    void tick(float dt_s, state& st)
    {
        for(character* c : characters)
        {
            c->tick(dt_s, st);
        }
    }
};*/

struct character_manager : virtual renderable_manager_base<character_base>, virtual collideable_manager_base<character_base>, virtual network_manager_base<character_base>
{
    void tick(float dt, state& st)
    {
        for(character_base* c : objs)
        {
            c->tick(dt, st);
        }
    }

    virtual ~character_manager(){}
};

#endif // CHARACTER_HPP_INCLUDED
