//
// client_impl.cpp
//
// Author: 	Lutz Bichler
//
// This file is part of the BMW Some/IP implementation.
//
// Copyright © 2013, 2014 Bayerische Motoren Werke AG (BMW).
// All rights reserved.
//

#include <boost/asio/placeholders.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/back/state_machine.hpp>

#include <vsomeip/service_discovery/internal/client_behavior_impl.hpp>
#include <vsomeip/service_discovery/internal/events.hpp>

namespace vsomeip {
namespace service_discovery {

namespace msm = boost::msm;
namespace mpl = boost::mpl;
using namespace boost::msm::front;

////////////////////////////////////////////////////////
/////////// Implementation of statechart       /////////
////////////////////////////////////////////////////////

// Top-level state machine
struct client_behavior_def
		: public msm::front::state_machine_def<	client_behavior_def> {

	~client_behavior_def() { delete timer_; };

	client_behavior_impl *owner_;

	// members
	bool is_configured_;
	bool is_requested_;

	uint32_t run_;
	uint32_t max_run_;

	boost::asio::system_timer *timer_;

	// methods
	void init_timer(boost::asio::io_service &_is) {
		timer_ = new boost::asio::system_timer(_is);
		run_ = 0;
		max_run_ = 3;
	}

	void set_timer(uint32_t _milliseconds) {
		std::cout << "set_timer(" << _milliseconds << ")" << std::endl;
		timer_->expires_from_now(
				std::chrono::milliseconds(_milliseconds));
		timer_->async_wait(boost::bind(
				&client_behavior_def::timeout_expired,
				this,
				boost::asio::placeholders::error));
	}

	void cancel_timer() {
		timer_->cancel();
	}

	void timeout_expired(const boost::system::error_code &_error_code) {
		std::cout << "Timer expired (" << _error_code << ")" << std::endl;
		if (!_error_code) {
			owner_->process_event(ev_timeout_expired());
		}
	}

	// guards
	bool is_configured_and_requested(none const &_event) {
		return (is_configured_ && is_requested_);
	}

	bool is_configured_and_requested(
			ev_configuration_status_change const &_event) {
		set_configured(_event);
		return (_event.is_configured_ && is_requested_);
	}

	bool is_configured_and_requested(
			ev_request_change const &_event) {
		set_requested(_event);
		return (is_configured_ && _event.is_requested_);
	}

	bool is_configured() const {
		return is_configured_;
	}

	bool is_requested(none const &_event) {
		return is_requested_;
	}

	bool is_requested(ev_request_change const &_event) {
		return _event.is_requested_;
	}

	bool is_repeating(ev_timeout_expired const &_event) const {
		return (run_ < max_run_);
	}

	// actions
	void set_configured(ev_configuration_status_change const &_event) {
		is_configured_ = _event.is_configured_;
	}

	bool set_requested(ev_request_change const &_event) {
		is_requested_ = _event.is_requested_;
	}

	// States
	struct unavailable_def
		: public msm::front::state_machine_def<unavailable_def> {

		struct client_behavior_def *owner_;

		void set_owner(struct client_behavior_def *_owner) {
			owner_ = _owner;
		}

		template<class Event, class Fsm>
		void on_entry(Event const &, Fsm &_fsm) {
			std::cout << "unavailable_def" << std::endl;
			_fsm.run_ = 0;
		}

		// States
		struct waiting
			: public msm::front::state<> {
			template<class Event, class Fsm>
			void on_entry(Event const &, Fsm &_fsm) {
				std::cout << "  waiting" << std::endl;
			}
		};

		struct initializing
			: public msm::front::state<> {
			template<class Event, class Fsm>
			void on_entry(Event const &, Fsm &_fsm) {
				_fsm.owner_->cancel_timer();
				std::cout << "  initializing" << std::endl;
				_fsm.owner_->set_timer(1000);
			}
		};

		struct searching
			: public msm::front::state<> {
			template<class Event, class Fsm>
			void on_entry(Event const &, Fsm &_fsm) {
				std::cout << "  searching" << std::endl;
				_fsm.owner_->cancel_timer();
				_fsm.owner_->owner_->find_service();
				_fsm.owner_->run_++;
				_fsm.owner_->set_timer((2<<_fsm.owner_->run_) * 1000);
			}
		};

		// Guards
		bool is_repeating(ev_timeout_expired const &_event) {
			return (owner_->is_repeating(_event));
		}

		bool is_up_and_requested(none const &_event) {
			return (owner_->is_configured_and_requested(_event));
		}

		bool is_up_and_requested(ev_configuration_status_change const &_event) {
			std::cout << "Received configuration change" << std::endl;
			set_configured(_event);
			return (owner_->is_configured_and_requested(_event));
		}

		bool is_up_and_requested(ev_request_change const &_event) {
			std::cout << "Received request change" << std::endl;
			set_requested(_event);
			return (owner_->is_configured_and_requested(_event));
		}

		// Actions
		void set_configured(ev_configuration_status_change const &_event) {
			owner_->set_configured(_event);
		}

		void set_requested(ev_request_change const &_event) {
			owner_->set_requested(_event);
		}

		struct transition_table : mpl::vector<
			g_row< waiting, none, initializing,
				   &unavailable_def::is_up_and_requested >,
			  row< waiting, ev_configuration_status_change, initializing,
			  	   &unavailable_def::set_configured,
			  	   &unavailable_def::is_up_and_requested >,
			  row< waiting, ev_request_change, initializing,
			  	   &unavailable_def::set_requested,
			  	   &unavailable_def::is_up_and_requested >,
			 _row< initializing, ev_timeout_expired, searching >,
			g_row< searching, ev_timeout_expired, searching,
			       &unavailable_def::is_repeating >
		> {};

		typedef waiting initial_state;

		template <class Fsm, class Event>
		void no_transition(Event const &_event, Fsm &_machine, int state) {
			// TODO: log "Received illegal event!"
		}
	};

	// Use back end to make the inner state machine work
	typedef msm::back::state_machine< unavailable_def > unavailable;

	struct available_def
		: public msm::front::state_machine_def<available_def> {

		struct client_behavior_def *owner_;

		void set_owner(struct client_behavior_def *_owner) {
			owner_ = _owner;
		}

		struct unrequested
			: public msm::front::state<> {
			template<class Event, class Fsm>
			void on_entry(Event const &, Fsm &) {
				std::cout << "  unrequested" << std::endl;
			}
		};

		struct requested
			: public msm::front::state<> {
			template<class Event, class Fsm>
			void on_entry(Event const &, Fsm &_fsm) {
				std::cout << "  requested" << std::endl;
			}
		};

		struct exit_on_down
			: public exit_pseudo_state<none> {
			template<class Event, class Fsm>
			void on_entry(Event const &, Fsm &_fsm) {
				std::cout << "  exit_on_down" << std::endl;
			}
		};

		template<class Event, class Fsm>
		void on_entry(Event const &, Fsm &_fsm) {
			std::cout << "available_def" << std::endl;
		}

		// Guards
		bool is_requested(none const &_event) {
			return owner_->is_requested(_event);
		}

		bool is_requested(ev_request_change const &_event) {
			return owner_->is_requested(_event);
		}

		bool is_down(ev_configuration_status_change const &_event) {
			return !_event.is_configured_;
		}

		bool is_up(ev_configuration_status_change const &_event) {
			return _event.is_configured_;
		}

		// Actions
		void set_requested(ev_request_change const &_event) {
			owner_->is_requested_ = _event.is_requested_;
		}

		void set_configured(ev_configuration_status_change const &_event) {
			owner_->is_configured_ = _event.is_configured_;
		}

		struct transition_table : mpl::vector<
			g_row< unrequested, none, requested,
			       &available_def::is_requested >,
			  row< unrequested, ev_request_change, requested,
			  	   &available_def::set_requested,
			  	   &available_def::is_requested >,
			  row< unrequested, ev_configuration_status_change, exit_on_down,
			  	   &available_def::set_configured,
			       &available_def::is_down >,
			 _row< requested, ev_offer_service, requested >
		> {};

		typedef unrequested initial_state;

		template <class Fsm, class Event>
		void no_transition(Event const &_event, Fsm &_machine, int state) {
			// TODO: log "Received illegal event!"
		}
	};

	// Use back end to make the inner state machine work
	typedef msm::back::state_machine<available_def> available;

	struct transition_table : mpl::vector<
		_row< unavailable, ev_offer_service, available >,
		_row< available::exit_pt< available_def::exit_on_down >, none, unavailable >,
		_row< available, ev_timeout_expired, available >,
		_row< available, ev_stop_offer_service, unavailable >
	> {};

	typedef unavailable initial_state;

	template <class Fsm, class Event>
	void no_transition(Event const &_event, Fsm &_machine, int state) {
		// TODO: log "Received illegal event!"
	}
};

////////////////////////////////////////////////////////
/////////// Implementation of members          /////////
////////////////////////////////////////////////////////
struct client_behavior_impl::state_machine
		: msm::back::state_machine< client_behavior_def > {
};

////////////////////////////////////////////////////////
/////////// Implementation of member functions /////////
////////////////////////////////////////////////////////
client_behavior_impl::client_behavior_impl(boost::asio::io_service& _is)
	: state_machine_(new state_machine) {

	state_machine::unavailable& unavailable_state
		= state_machine_->get_state<state_machine::unavailable&>();
	unavailable_state.set_owner(state_machine_.get());

	state_machine::available& available_state
		= state_machine_->get_state<state_machine::available&>();
	available_state.set_owner(state_machine_.get());

	state_machine_->init_timer(_is);

	state_machine_->owner_ = this;
}

void client_behavior_impl::start() {
	state_machine_->start();
}

void client_behavior_impl::stop() {
	state_machine_->stop();
}

void client_behavior_impl::process_event( event_variant e)
{
	boost::apply_visitor(ProcessEvent<client_behavior_impl>(*this), e);
}

template<class Event>
void client_behavior_impl::process_event(const Event &e) {
	state_machine_->process_event(e);
}

} // namespace service_discovery
} // namespace vsomeip