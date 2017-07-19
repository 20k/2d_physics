#ifndef MANAGERS_HPP_INCLUDED
#define MANAGERS_HPP_INCLUDED

#include <stdint.h>
#include <vector>
#include "networking.hpp"
#include "networkable_systems.hpp"

struct renderable;
struct projectile;
struct state;
struct network_state;

///shared state
///every object has a unique id globally
extern uint16_t o_id;

template<typename T>
struct object_manager
{
    int16_t system_network_id = -1;

    std::vector<T*> objs;

    template<typename real_type, typename... U>
    T* make_new(U... u)
    {
        T* nt = new real_type(u...);

        nt->object_id = o_id++;

        objs.push_back(nt);

        return nt;
    }

    void destroy(T* t)
    {
        for(int i=0; i<objs.size(); i++)
        {
            if(objs[i] == t)
            {
                objs.erase(objs.begin() + i);
                i--;

                delete t;
                continue;
            }
        }
    }

    void add(T* t)
    {
        objs.push_back(t);
    }

    void rem(T* t)
    {
        for(int i=0; i<objs.size(); i++)
        {
            if(objs[i] == t)
            {
                objs.erase(objs.begin() + i);
                i--;
            }
        }
    }

    void erase_all()
    {
        /*for(auto& i : objs)
        {
            delete i;
        }*/

        objs.clear();
    }

    void cleanup(state& st)
    {
        for(int i=0; i<objs.size(); i++)
        {
            if(objs[i]->should_cleanup)
            {
                objs[i]->on_cleanup(st);

                objs.erase(objs.begin() + i);
                i--;

                continue;
            }
        }
    }

    bool owns(int id, int32_t ownership_class)
    {
        for(auto& i : objs)
        {
            if(i->object_id == id && i->ownership_class == ownership_class)
                return true;
        }

        return false;
    }
};

template<typename T>
struct network_manager_base : virtual object_manager<T>
{
    ///real type is the type to create if we receive a new networked entity
    template<typename manager_type, typename real_type>
    void tick_create_networking(network_state& ns)
    {
        manager_type* type = dynamic_cast<manager_type*>(this);

        ///when reading this, ignore the template keyword
        ///its because this is a dependent type
        ns.template check_create_network_entity<manager_type, real_type>(*type);
    }

    void update_network_entities(network_state& ns)
    {
        for(T* obj : object_manager<T>::objs)
        {
            networkable_host* host_object = dynamic_cast<networkable_host*>(obj);

            if(host_object != nullptr)
            {
                host_object->update(ns, object_manager<T>::system_network_id);
                host_object->process_recv(ns);
            }

            networkable_client* client_object = dynamic_cast<networkable_client*>(obj);

            if(client_object != nullptr)
            {
                client_object->update(ns, object_manager<T>::system_network_id);
                client_object->process_recv(ns);
            }
        }
    }

    template<typename manager_type, typename real_type>
    void tick_all_networking(network_state& ns)
    {
        tick_create_networking<manager_type, real_type>(ns);

        update_network_entities(ns);
    }

    virtual ~network_manager_base(){}
};

template<typename T>
struct renderable_manager_base : virtual object_manager<T>
{
    virtual void render(sf::RenderWindow& win)
    {
        for(renderable* r : object_manager<T>::objs)
        {
            r->render(win);
        }
    }
};

struct renderable_manager : virtual renderable_manager_base<renderable>
{

};

template<typename T>
struct collideable_manager_base : virtual object_manager<T>
{
    template<typename U>
    void check_collisions(state& st, collideable_manager_base<U>& other)
    {
        for(int i=0; i<object_manager<T>::objs.size(); i++)
        {
            T* my_t = object_manager<T>::objs[i];

            for(int j=0; j<other.objs.size(); j++)
            {
                U* their_t = other.objs[j];

                /*if(my_t->intersects(their_t))
                {
                    printf("%i %i\n", my_t->team, their_t->team);
                }*/

                if(!my_t->can_collide() || !their_t->can_collide())
                    continue;

                if((void*)my_t == (void*)their_t)
                    continue;

                if(my_t->intersects(their_t))
                {
                    my_t->on_collide(st, their_t);
                    their_t->on_collide(st, my_t);
                }
            }
        }
    }
};

struct timestep_state
{
    //#define NO_FIXED

    float get_max_step(float dt_s)
    {
        #ifndef NO_FIXED
        return 8.f * (1/1000.f);
        #else
        return dt_s;
        #endif
    }

    float saved_timestep = 0.f;

    int step(float dt_s)
    {
        #ifndef NO_FIXED
        saved_timestep += dt_s;

        int num = floor(saved_timestep / get_max_step(dt_s));

        num = clamp(num, 0, 1);

        saved_timestep -= num * get_max_step(dt_s);

        return num;
        #else
        return 1;
        #endif
    }
};

template<typename T>
struct chemical_interaction_base : virtual object_manager<T>
{
    timestep_state fts;

    template<typename U>
    void check_interaction(float dt_s, state& st, chemical_interaction_base<U>& other)
    {
        int nsteps = fts.step(dt_s);

        for(int kk=0; kk < nsteps; kk++)
        {
            for(int i=0; i<object_manager<T>::objs.size(); i++)
            {
                T* my_t = object_manager<T>::objs[i];

                my_t->interact(fts.get_max_step(dt_s), other);
            }
        }

        /*cur_timestep = clamp(cur_timestep, 0.00001f, max_timestep);

        for(int i=0; i<object_manager<T>::objs.size(); i++)
        {
            T* my_t = object_manager<T>::objs[i];

            my_t->interact(cur_timestep, other);
        }*/
    }
};

struct projectile_manager : virtual renderable_manager_base<projectile_base>, virtual collideable_manager_base<projectile_base>, virtual network_manager_base<projectile_base>
{
    void tick(float dt_s, state& st)
    {
        for(projectile_base* p : objs)
        {
            p->tick(dt_s, st);
        }
    }
};


#endif // MANAGERS_HPP_INCLUDED
