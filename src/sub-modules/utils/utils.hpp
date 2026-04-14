#ifndef UTILS_HPP
#define UTILS_HPP

#define IS_EVENT(events, check_event) \ 
    ((events & check_event) == check_event) ? true : false

#endif // UTILS_HPP