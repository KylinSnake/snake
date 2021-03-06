#ifndef SNAKE_CORE_THREAD_GROUP_T_H
#define SNAKE_CORE_THREAD_GROUP_T_H

#include <cassert>
#include <memory>
#include <vector>
#include <tuple>
#include <algorithm>
#include <type_traits>
#include <snake/concurrency/Mutex.h>
#include <snake/concurrency/ConditionVariable.h>
#include <snake/concurrency/Thread.h>
#include <snake/concurrency/Future.h>
#include <snake/concurrency/Atomic.h>

namespace snake
{
	template<class T
		, class R
		, template<class U, template<class S> class QueueT> class QueueGroupT
		, template<class V> class DispatchT
		, template<class W> class QueueT
	>
		class ThreadGroupT
	{
	public:
		using WorkFunction = std::function<R( T& )>;
		ThreadGroupT( WorkFunction&& func, size_t num = 1) 
			: func_(std::move(func))
			, num_(num)
			, queue_group_(num)
			, dispatcher_(num)
			, threads_(num)
			, flag_()
		{
			assert( num > 0 );
		}
		~ThreadGroupT()
		{
			stop();
		}
		ThreadGroupT( const ThreadGroupT & ) = delete;
		ThreadGroupT& operator=( const ThreadGroupT& ) = delete;

		Future<R> push( T&& t)
		{
			Promise<R> p;
			Future<R> f = p.get_future();
			if (queue_group_.get_queue( dispatcher_.next( t ) ).push( std::make_tuple( std::move( t ), std::move( p ) ) ))
			{
				return f;
			}
			return Future<R>();
		}

		void invoke( Promise<R> & p, T& t, std::true_type )
		{
			func_( t );
			p.set_value();
		}

		void invoke( Promise<R> & p, T& t, std::false_type )
		{
			p.set_value( func_( t ) );
		}

		void start()
		{
			for (size_t i = 0; i < num_; ++i)
			{
				QueueT<std::tuple<T, Promise<R>>>& queue = queue_group_.get_queue( i );
				threads_[i].reset( new Thread( [&queue, this]
				{
					std::tuple<T, Promise<R>> t;
					while (queue.pop( t ))
					{
						try
						{
							//set_future_value<R, WorkFunction>( std::get<1>( t ), func_, std::get<0>( t ) );
							invoke( std::get<1>( t ), std::get<0>( t ), std::is_void<R>() );
							//std::get<1>(t).set_value( this->func_( std::get<0>(t) ));
						}
						catch (...)
						{
							try
							{
								std::get<1>( t ).set_exception( std::current_exception() );
							}
							catch (...){}
						}
					}
					queue.clear( [] ( std::tuple<T, Promise<R>>& t )
					{
						std::get<1>( t ).set_exception( std::make_exception_ptr( std::bad_exception() ) );
					} );
				}) );
			}
		}
		void stop()
		{
			if (!flag_.test_and_set())
			{
				for (size_t i = 0; i < threads_.size(); ++i)
				{
					queue_group_.get_queue( i ).stop();
				}
				for (auto& t : threads_)
				{
					t->join();
				}
				threads_.clear();
			}
		}
	private:
		WorkFunction func_;
		size_t num_;
		QueueGroupT<std::tuple<T, Promise<R>>, QueueT> queue_group_;
		DispatchT<T> dispatcher_;
		std::vector<std::unique_ptr<Thread>> threads_;
		AtomicFlag flag_;
	};
}

#endif
