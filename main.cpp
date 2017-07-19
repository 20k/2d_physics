#include <iostream>
#include <fstream>
#include <vec/vec.hpp>

#include <SFML/Graphics.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui-SFML.h>
#include "util.hpp"
#include <net/shared.hpp>
#include <set>
#include "camera.hpp"
#include "state.hpp"
#include "systems.hpp"

#include "networking.hpp"

#include "networkable_systems.hpp"

bool suppress_mouse = false;

#include "managers.hpp"

/*struct tickable
{
    static std::vector<tickable*> tickables;

    void add_tickable(tickable* t)
    {
        tickables.push_back(t);
    }

    virtual void tick(float dt_s, state& st);

    static void tick_all(float dt_s, state& st)
    {
        for(tickable* t : tickables)
        {
            t->tick(dt_s, st);
        }
    }
};*/

struct physics_barrier : virtual renderable, virtual collideable, virtual base_class
{
    vec2f p1;
    vec2f p2;

    ///connected to p1
    physics_barrier* next = nullptr;
    ///connected to p2
    physics_barrier* prev = nullptr;

    physics_barrier() : collideable(-1, collide::PHYS_LINE) {}

    bool intersects(collideable* other)
    {
        if(other->type != collide::RAD)
            return false;

        if(crosses(other->collision_pos, other->last_collision_pos))
        {
            return true;
        }

        return false;
    }

    virtual void render(sf::RenderWindow& win)
    {
        sf::RectangleShape rect;

        float width = (p2 - p1).length();
        float height = 5.f;

        rect.setSize({width, height});

        float angle = (p2 - p1).angle();

        rect.setRotation(r2d(angle));

        rect.setPosition(p1.x(), p1.y());

        win.draw(rect);
    }

    int side(vec2f pos)
    {
        vec2f line = (p2 - p1).norm();

        vec2f normal = perpendicular(line);

        float res = dot(normal.norm(), (pos - (p1 + p2)/2.f).norm());

        if(res > 0)
            return 1;

        return -1;
    }

    static int side(vec2f pos, vec2f pos_1, vec2f pos_2)
    {
        vec2f line = (pos_2 - pos_1).norm();

        vec2f normal = perpendicular(line);

        float res = dot(normal.norm(), (pos - (pos_1 + pos_2)/2.f).norm());

        if(res > 0)
            return 1;

        return -1;
    }

    float fside(vec2f pos)
    {
        vec2f line = (p2 - p1).norm();

        vec2f normal = perpendicular(line);

        float res = dot(normal.norm(), (pos - (p1 + p2)/2.f).norm());

        return res;
    }

    static float fside(vec2f pos, vec2f pos_1, vec2f pos_2)
    {
        vec2f line = (pos_2 - pos_1).norm();

        vec2f normal = perpendicular(line);

        float res = dot(normal.norm(), (pos - (pos_1 + pos_2)/2.f).norm());

        return res;
    }

    bool opposite(float f1, float f2)
    {
        if(f1 == 0.f || f2 == 0.f)
            return true;

        if(signum(f1) != signum(f2))
        {
            return true;
        }

        return false;
    }

    bool crosses(vec2f pos, vec2f next_pos)
    {
        float s1 = fside(pos);
        float s2 = fside(next_pos);

        vec2f normal = get_normal();

        if(opposite(s1, s2))
        {
            vec2f n1 = p1 + normal;
            vec2f n2 = p2 + normal;

            float sn1 = physics_barrier::fside(pos, p1, n1);
            float sn2 = physics_barrier::fside(pos, p2, n2);

            float nn1 = physics_barrier::fside(next_pos, p1, n1);
            float nn2 = physics_barrier::fside(next_pos, p2, n2);

            if(opposite(sn1, sn2) || opposite(nn1, nn2))
            {
                return true;
            }

            vec2f intersect = point2line_intersection(pos, next_pos, p1, p2);

            float in1 = physics_barrier::fside(intersect, p1, n1);
            float in2 = physics_barrier::fside(intersect, p2, n2);

            if(opposite(in1, in2))
                return true;
        }

        return false;
    }

    bool crosses_normal(vec2f pos, vec2f next_pos)
    {
        return crosses(pos, next_pos) && on_normal_side(pos);
    }

    bool within(vec2f pos)
    {
        vec2f normal = get_normal();

        vec2f n1 = p1 + normal;
        vec2f n2 = p2 + normal;

        float sn1 = physics_barrier::fside(pos, p1, n1);
        float sn2 = physics_barrier::fside(pos, p2, n2);

        if(opposite(sn1, sn2))
        {
            return true;
        }

        return false;
    }

    vec2f get_normal()
    {
        return -perpendicular((p2 - p1).norm());
    }

    bool on_normal_side(vec2f pos)
    {
        if(!opposite(fside(pos), fside(p1 + get_normal())))
            return true;

        return false;
    }

    bool on_normal_side_with_default(vec2f pos, bool is_default)
    {
        ///testing if we're on the default side
        if(is_default)
        {
            return on_normal_side(pos);
        }
        else
        {
            return !on_normal_side(pos);
        }
    }

    vec2f get_normal_towards(vec2f pos)
    {
        vec2f rel = (pos - (p1 + p2)/2.f);

        vec2f n_1 = perpendicular(p2 - p1).norm();
        vec2f n_2 = -perpendicular(p2 - p1).norm();

        float a1 = angle_between_vectors(n_1, rel);
        float a2 = angle_between_vectors(n_2, rel);

        if(fabs(a1) < fabs(a2))
        {
            return n_1;
        }

        return n_2;
    }

    byte_vector serialise()
    {
        byte_vector vec;
        vec.push_back<vec2f>(p1);
        vec.push_back<vec2f>(p2);

        return vec;
    }

    void deserialise(byte_fetch& fetch)
    {
        p1 = fetch.get<vec2f>();
        p2 = fetch.get<vec2f>();
    }
};

struct physics_barrier_manager : virtual renderable_manager_base<physics_barrier>, virtual collideable_manager_base<physics_barrier>
{
    bool adding = false;
    vec2f adding_point;

    bool show_normals = false;

    void add_point(vec2f pos, state& st)
    {
        if(!adding)
        {
            adding_point = pos;

            adding = true;

            return;
        }

        if(adding)
        {
            vec2f p2 = pos;

            physics_barrier* bar = make_new<physics_barrier>();
            bar->p1 = adding_point;
            bar->p2 = p2;

            adding = false;

            return;
        }

        build_connectivity();
    }

    byte_vector serialise()
    {
        byte_vector vec;

        for(physics_barrier* bar : objs)
        {
            vec.push_vector(bar->serialise());
        }

        return vec;
    }

    void deserialise(byte_fetch& fetch, int num_bytes)
    {
        objs.clear();

        for(int i=0; i<num_bytes / (sizeof(vec2f) * 2); i++)
        {
            physics_barrier* bar = new physics_barrier;

            bar->deserialise(fetch);

            objs.push_back(bar);
        }

        build_connectivity();
    }

    bool any_crosses(vec2f p1, vec2f p2)
    {
        for(physics_barrier* bar : objs)
        {
            if(bar->crosses(p1, p2))
                return true;
        }

        return false;
    }

    bool any_crosses_normal(vec2f p1, vec2f p2)
    {
        for(physics_barrier* bar : objs)
        {
            if(bar->crosses_normal(p1, p2))
                return true;
        }

        return false;
    }

    virtual void render(sf::RenderWindow& win)
    {
        for(renderable* r : object_manager<physics_barrier>::objs)
        {
            r->render(win);
        }

        if(show_normals)
        {
            for(physics_barrier* bar : objs)
            {
                vec2f normal = bar->get_normal();

                sf::RectangleShape rect;

                rect.setSize({20, 2});

                rect.setOrigin({0, 1});
                rect.setFillColor(sf::Color(255, 100, 100));

                vec2f center = (bar->p1 + bar->p2)/2.f;

                rect.setPosition(center.x(), center.y());

                rect.setRotation(r2d(normal.angle()));

                win.draw(rect);
            }
        }
    }

    void build_connectivity()
    {
        for(physics_barrier* b1 : objs)
        {
            for(physics_barrier* b2 : objs)
            {
                if(b1 == b2)
                    continue;

                if(b1->p1 == b2->p2)
                {
                    b1->prev = b2;
                    b2->next = b1;
                }

                if(b1->p2 == b2->p1)
                {
                    b1->next = b2;
                    b2->prev = b1;
                }
            }
        }
    }
};

struct game_world_manager
{
    int16_t system_network_id = -1;

    int cur_spawn = 0;
    std::vector<vec2f> spawn_positions;

    bool should_render = false;

    void add(vec2f pos)
    {
        spawn_positions.push_back(pos);
    }

    void render(sf::RenderWindow& win)
    {
        if(!should_render)
            return;

        float rad = 8;

        sf::CircleShape circle;
        circle.setRadius(rad);
        circle.setOrigin(rad, rad);
        circle.setFillColor(sf::Color(255, 128, 255));

        for(vec2f& pos : spawn_positions)
        {
            circle.setPosition({pos.x(), pos.y()});
            win.draw(circle);
        }
    }

    void enable_rendering()
    {
        should_render = true;
    }

    void disable_rendering()
    {
        should_render = false;
    }

    vec2f get_next_spawn()
    {
        if(spawn_positions.size() == 0)
            return {0,0};

        vec2f pos = spawn_positions[cur_spawn];

        cur_spawn = (cur_spawn + 1) % spawn_positions.size();

        return pos;
    }

    byte_vector serialise()
    {
        byte_vector ret;

        for(vec2f& pos : spawn_positions)
        {
            ret.push_back<vec2f>(pos);
        }

        return ret;
    }

    void deserialise(byte_fetch& fetch, int num_bytes)
    {
        spawn_positions.clear();

        for(int i=0; i<num_bytes / sizeof(vec2f); i++)
        {
            spawn_positions.push_back(fetch.get<vec2f>());
        }
    }
};

#include "character.hpp"

struct debug_controls
{
    int controls_state = 0;

    int tools_state = 0;

    float zoom_level = 1.f;

    void zoom(float amount)
    {
        if(amount > 0)
        {
            //zoom_level = pow(zoom_level, amount + 1);

            zoom_level *= pow(1.3, amount + 1);
        }
        else if(amount < 0)
        {
            zoom_level /= pow(1.3, fabs(amount) + 1);
        }
    }

    void line_draw_controls(vec2f mpos, state& st)
    {
        if(suppress_mouse)
            return;

        if(ONCE_MACRO(sf::Mouse::Left))
        {
            st.physics_barrier_manage.add_point(mpos, st);
        }
    }

    void spawn_controls(vec2f mpos, state& st)
    {
        if(suppress_mouse)
            return;

        if(ONCE_MACRO(sf::Mouse::Left))
        {
            st.game_world_manage.add(mpos);
        }
    }

    /*vec2f last_upper = {0,0};
    vec2f last_lower = {0,0};

    bool has_last = false;

    void connected_line_tool(vec2f mpos, state& st)
    {
        if(suppress_mouse)
            return;

        float separation = 10.f;

        if(ONCE_MACRO(sf::Mouse::Left))
        {
            vec2f

            vec2f up;
            vec2f down;

            if(!has_last)
            {
                up = (vec2f){0, 1} * separation/2.f;
                down = -up;
            }

            vec2f upos = mpos + up;
            vec2f dpos = mpos + down;

            if(has_last)
            {
                st.physics_barrier_manage.add_point(last_upper, st);
                st.physics_barrier_manage.add_point(upos, st);

                st.physics_barrier_manage.add_point(last_lower, st);
                st.physics_barrier_manage.add_point(dpos, st);
            }

            has_last = true;

            last_upper = upos;
            last_lower = dpos;
        }
    }*/

    vec2f last_drag_pos;
    bool has_last_drag = false;

    void drag_line_tool(vec2f mpos, state& st)
    {
        if(suppress_mouse)
            return;

        sf::Mouse mouse;

        if(!mouse.isButtonPressed(sf::Mouse::Left))
        {
            has_last_drag = false;

            return;
        }

        float plonk_distance = 20.f;

        if(has_last_drag && (mpos - last_drag_pos).length() > plonk_distance)
        {
            st.physics_barrier_manage.add_point(last_drag_pos, st);
            st.physics_barrier_manage.add_point(mpos, st);

            last_drag_pos = mpos;
        }

        if(!has_last_drag)
            last_drag_pos = mpos;

        has_last_drag = true;
    }

    vec2f last_pos;
    bool has_last = false;

    void connected_line_tool(vec2f mpos, state& st)
    {
        if(suppress_mouse)
            return;

        //float separation = 10.f;

        if(ONCE_MACRO(sf::Mouse::Left))
        {
            if(has_last)
            {
                st.physics_barrier_manage.add_point(last_pos, st);
                st.physics_barrier_manage.add_point(mpos, st);
            }

            has_last = true;
            last_pos = mpos;
        }
    }

    vec2f last_spawn_pos;
    bool has_last_spawn = false;

    void spawn_continuous(vec2f mpos, state& st)
    {
        if(suppress_mouse)
            return;

        sf::Mouse mouse;

        if(mouse.isButtonPressed(sf::Mouse::Left) && !has_last_spawn)
        {
            last_spawn_pos = mpos;
            has_last_spawn = true;
        }

        if(mouse.isButtonPressed(sf::Mouse::Left))
        {
            vec2f dist = (mpos - last_spawn_pos);

            float spacing = 20.f;

            if(dist.length() > spacing)
            {
                physics_object_host* c = dynamic_cast<physics_object_host*>(st.physics_object_manage.make_new<physics_object_host>(1, st.net_state));

                c->pos = mpos;
                c->last_pos = c->pos;
                c->init_collision_pos(c->pos);

                last_spawn_pos = mpos;
            }
        }
    }

    bool show_normals = false;

    void editor_controls(vec2f mpos, state& st)
    {
        st.game_world_manage.enable_rendering();

        ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        std::vector<std::string> tools{"Line Draw", "Spawn Point", "Connected Line Tool", "Drag Line Tool", "Spawn", "Spawn Cont"};

        for(int i=0; i<tools.size(); i++)
        {
            std::string str = tools[i];

            if(tools_state == i)
            {
                str += " <--";
            }

            if(ImGui::Button(str.c_str()))
            {
                tools_state = i;
            }
        }

        if(tools_state == 0)
        {
            line_draw_controls(mpos, st);
        }

        if(tools_state == 1)
        {
            spawn_controls(mpos, st);
        }

        if(tools_state == 2)
        {
            connected_line_tool(mpos, st);
        }

        if(tools_state == 3)
        {
            drag_line_tool(mpos, st);
        }

        /*if(ImGui::Button("Spawn"))
        {
            physics_object_host* c = dynamic_cast<physics_object_host*>(st.physics_object_manage.make_new<physics_object_host>(1, st.net_state));

            c->pos = mpos;
            c->last_pos = c->pos;
            c->init_collision_pos(c->pos);
        }*/

        if(tools_state == 4)
        {
            if(ONCE_MACRO(sf::Mouse::Left) && !suppress_mouse)
            {
                physics_object_host* c = dynamic_cast<physics_object_host*>(st.physics_object_manage.make_new<physics_object_host>(1, st.net_state));

                c->pos = mpos;
                c->last_pos = c->pos;
                c->init_collision_pos(c->pos);
            }
        }

        if(tools_state == 5)
        {
            spawn_continuous(mpos, st);
        }

        ImGui::Checkbox("Show normals", &show_normals);

        st.physics_barrier_manage.show_normals = show_normals;

        ImGui::End();
    }

    void tick(state& st)
    {
        vec2f mpos = st.cam.get_mouse_position_world();

        st.game_world_manage.disable_rendering();

        ImGui::Begin("Control menus");

        std::vector<std::string> modes{"Editor", "Run"};

        for(int i=0; i<modes.size(); i++)
        {
            std::string str = modes[i];

            if(controls_state == i)
            {
                str += " <--";
            }

            if(ImGui::Button(str.c_str()))
            {
                controls_state = i;
            }
        }

        if(controls_state == 0)
        {
            editor_controls(mpos, st);
        }

        ImGui::End();
    }
};

void save(const std::string& file, physics_barrier_manager& physics_barrier_manage, game_world_manager& game_world_manage)
{
    byte_vector v1 = physics_barrier_manage.serialise();
    byte_vector v2 = game_world_manage.serialise();

    std::ofstream fout;
    fout.open(file, std::ios::binary | std::ios::out);

    int32_t v1_s = v1.ptr.size();
    int32_t v2_s = v2.ptr.size();

    fout.write((char*)&v1_s, sizeof(int32_t));
    fout.write((char*)&v2_s, sizeof(int32_t));

    if(v1.ptr.size() > 0)
        fout.write((char*)&v1.ptr[0], v1.ptr.size());

    if(v2.ptr.size() > 0)
        fout.write((char*)&v2.ptr[0], v2.ptr.size());
}

byte_fetch get_file(const std::string& fname)
{
    // open the file:
    std::ifstream file(fname, std::ios::binary);

    file.seekg(0, std::ios::end);
    auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    byte_fetch ret;

    if(file_size > 0)
    {
        ret.ptr.resize(file_size);
        file.read((char*)&ret.ptr[0], file_size);
    }

    return ret;
}

void load(const std::string& file, physics_barrier_manager& physics_barrier_manage, game_world_manager& game_world_manage, renderable_manager& renderable_manage)
{
    byte_fetch fetch = get_file(file);

    int32_t v1_s = fetch.get<int32_t>();
    int32_t v2_s = fetch.get<int32_t>();

    renderable_manage.erase_all();

    physics_barrier_manage.deserialise(fetch, v1_s);
    game_world_manage.deserialise(fetch, v2_s);
}

int main()
{
    networking_init();

    sf::ContextSettings context(0, 0, 8);

    sf::RenderWindow win;
    win.create(sf::VideoMode(1500, 1000), "fak u mark", sf::Style::Default, context);

    ImGui::SFML::Init(win);
    ImGui::NewFrame();

    renderable_manager renderable_manage;
    physics_object_manager physics_object_manage;

    physics_barrier_manager physics_barrier_manage;
    game_world_manager game_world_manage;

    projectile_manager projectile_manage;

    camera cam(win);

    network_state net_state;

    debug_controls controls;

    state st(physics_object_manage, physics_barrier_manage, game_world_manage, renderable_manage, projectile_manage, cam, net_state);

    st.physics_object_manage.system_network_id = 0;
    st.physics_barrier_manage.system_network_id = 1;
    st.game_world_manage.system_network_id = 2;
    st.renderable_manage.system_network_id = 3;
    st.projectile_manage.system_network_id = 4;

    ///-2 team bit of a hack, objects default to -1
    //player_character* test = dynamic_cast<player_character*>(character_manage.make_new<player_character>(-2, st.net_state));

    load("file.mapfile", physics_barrier_manage, game_world_manage, renderable_manage);
    //renderable_manage.add(test);

    sf::Clock clk;

    sf::Keyboard key;
    sf::Mouse mouse;

    sf::sleep(sf::milliseconds(1));

    uint32_t frame = 0;

    while(win.isOpen())
    {
        auto sfml_mpos = mouse.getPosition(win);

        vec2f mpos = {sfml_mpos.x, sfml_mpos.y};

        float dt_s = (clk.restart().asMicroseconds() / 1000.) / 1000.f;

        if(dt_s > 1/33.f)
        {
            dt_s = 1/33.f;
        }

        st.dt_s = dt_s;

        sf::Event event;

        float scrollwheel_delta = 0;

        while(win.pollEvent(event))
        {
            ImGui::SFML::ProcessEvent(event);

            if(event.type == sf::Event::Closed)
            {
                win.close();
            }

            if(event.type == sf::Event::MouseWheelScrolled && event.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel)
                scrollwheel_delta -= event.mouseWheelScroll.delta;
        }

        if(fabs(scrollwheel_delta) > 0 && win.hasFocus() && !suppress_mouse)
        {
            controls.zoom(scrollwheel_delta);
        }

        cam.set_zoom(controls.zoom_level);

        ImGui::SFML::Update(clk.restart());

        const ImGuiIO& io = ImGui::GetIO();

        suppress_mouse = false;

        if(io.WantCaptureMouse)
            suppress_mouse = true;

        vec2f move_dir;

        float mult = 1000.f;

        if(win.hasFocus())
        {
            move_dir.x() += (int)key.isKeyPressed(sf::Keyboard::D);
            move_dir.x() -= (int)key.isKeyPressed(sf::Keyboard::A);

            move_dir.y() += (int)key.isKeyPressed(sf::Keyboard::S);
            move_dir.y() -= (int)key.isKeyPressed(sf::Keyboard::W);

            //if(controls.controls_state == 0)
            {
                float mult = 1.f;

                if(key.isKeyPressed(sf::Keyboard::LShift))
                    mult = 10.f;

                cam.move_pos(move_dir * mult);
            }
        }

        controls.tick(st);

        if(controls.controls_state == 0)
        {
            ImGui::Begin("Save/Load");

            if(ImGui::Button("Save"))
            {
                save("file.mapfile", physics_barrier_manage, game_world_manage);
            }

            if(ImGui::Button("Load"))
            {
                load("file.mapfile", physics_barrier_manage, game_world_manage, renderable_manage);
            }

            ImGui::End();
        }

        if(controls.controls_state == 1)
        {
            if(frame > 1)
            {
                physics_object_manage.tick(dt_s, st);

                physics_object_manage.check_interaction(dt_s, st, physics_object_manage);

                physics_object_manage.resolve_barrier_collisions(dt_s, st);
            }

            projectile_manage.tick(dt_s, st);
        }

        if(controls.controls_state == 0)
        {
            if(frame > 1 && ONCE_MACRO(sf::Keyboard::Space))
            {
                physics_object_manage.tick(dt_s, st);

                physics_object_manage.check_interaction(dt_s, st, physics_object_manage);

                physics_object_manage.resolve_barrier_collisions(dt_s, st);
            }
        }

        net_state.tick_cleanup();
        net_state.tick_join_game(dt_s);
        net_state.tick();

        projectile_manage.check_collisions(st, physics_object_manage);
        projectile_manage.check_collisions(st, physics_barrier_manage);

        projectile_manage.tick_all_networking<projectile_manager, projectile>(net_state);
        physics_object_manage.tick_all_networking<physics_object_manager, physics_object_client>(net_state);

        cam.update_camera();

        win.clear();

        projectile_manage.cleanup(st);

        renderable_manage.render(win);
        physics_barrier_manage.render(win);
        game_world_manage.render(win);
        projectile_manage.render(win);
        physics_object_manage.render(win);


        ImGui::Render();
        win.display();

        sf::sleep(sf::milliseconds(1));

        frame++;
    }

    return 0;
}
