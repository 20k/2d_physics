#ifndef SYSTEMS_HPP_INCLUDED
#define SYSTEMS_HPP_INCLUDED

#include <SFML/Graphics.hpp>
#include <vec/vec.hpp>
#include <imgui/imgui.h>

#define GRAVITY_STRENGTH 1600.f
#define FORCE_MULTIPLIER 1.f

struct state;

struct base_class
{
    bool should_cleanup = false;
    int16_t object_id = -1;
    int16_t ownership_class = -1;

    virtual void on_cleanup(state& st) {}
};

struct renderable
{
    sf::Image img;
    sf::Texture tex;
    vec3f col = {1, 1, 1};

    bool should_render = true;

    renderable()
    {
        img.create(20, 20, sf::Color(255, 255, 255));
        tex.loadFromImage(img);

        generate_colour();
    }

    virtual void render(sf::RenderWindow& win, vec2f pos)
    {
        if(!should_render)
            return;

        sf::Sprite spr(tex);
        spr.setOrigin(tex.getSize().x/2, tex.getSize().y/2);
        spr.setPosition({pos.x(), pos.y()});
        spr.setColor(sf::Color(255 * col.x(), 255 * col.y(),255 * col.z()));

        win.draw(spr);
    }

    void generate_colour()
    {
        float ffrac = 0.7f;

        col = randf<3, float>() * ffrac + (1.f - ffrac);
    }

    virtual void render(sf::RenderWindow& win) = 0;

    virtual ~renderable()
    {

    }

    bool out_of_bounds(sf::RenderWindow& win, vec2f pos)
    {
        auto spos = win.mapCoordsToPixel({pos.x(), pos.y()});

        int hx = tex.getSize().x/2;
        int hy = tex.getSize().y/2;

        if(spos.x + hx < 0 || spos.y + hy < 0 || spos.x - hx > win.getSize().x || spos.y - hy > win.getSize().y)
            return true;

        return false;
    }
};

namespace collide
{
    enum type
    {
        NONE,
        RAD,
        PHYS_LINE,
    };
}

using collide_t = collide::type;

struct collideable : virtual base_class
{
    int team = 0;

    bool fully_init = false;
    uint32_t collision_state = 0;
    vec2f last_collision_pos;
    vec2f collision_pos;
    vec2f collision_dim = {2, 2};

    collide_t type;

    collideable(int t, collide_t _type)
    {
        team = t;
        type = _type;
    }

    virtual bool can_collide() {return true;}

    virtual void on_collide(state& st, collideable* other) {}

    virtual bool intersects(collideable* other)
    {
        //if (RectA.Left < RectB.Right && RectA.Right > RectB.Left &&
        //RectA.Top > RectB.Bottom && RectA.Bottom < RectB.Top )

        /*vec2f mhdim = collision_dim/2.f;
        vec2f thdim = other->collision_dim/2.f;

        vec2f Atl = collision_pos - mhdim;
        vec2f Abr = collision_pos + mhdim;

        vec2f Btl = other->collision_pos - thdim;
        vec2f Bbr = other->collision_pos + thdim;

        std::cout << collision_pos << " " << other->collision_pos << std::endl;
        std::cout << Atl << " " << Abr << " " << Btl << " " << Bbr << std::endl;

        if(Atl.x() < Bbr.x() && Abr.x() > Btl.x() && Atl.y() > Bbr.y() && Abr.y() < Btl.y())
        {
            std::cout << "hello\n";

            return true;
        }

        return false;*/

        if(type == collide::RAD && other->type == collide::RAD)
        {
            vec2f diff = collision_pos - other->collision_pos;

            if(diff.length() < collision_dim.length()/2.f || diff.length() < other->collision_dim.length()/2.f)
                return true;

            return false;
        }

        if(type == collide::RAD && other->type == collide::PHYS_LINE)
            return other->intersects(this);

        /*if(type == collide::PHYS_LINE && other->type == collide::RAD)
        {
            return
        }*/

        printf("unsupported collider type %i\n", type);

        return false;
    }

    void set_collision_pos(vec2f pos)
    {
        last_collision_pos = collision_pos;
        collision_pos = pos;
    }

    void init_collision_pos(vec2f pos)
    {
        last_collision_pos = pos;
        collision_pos = pos;
    }

    virtual ~collideable()
    {

    }
};

struct moveable : virtual base_class
{
    vec2f pos;

    bool has_default = false;
    bool on_default_side = false;
    float side_time = 0.f;
    float side_time_max = 0.100f;
};

#endif // SYSTEMS_HPP_INCLUDED
