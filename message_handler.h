#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#define UNUSED __attribute__ ((unused))

class Message_Handler {
public:
    friend class Format_Reader;
    virtual ~Message_Handler() { }

    virtual bool configure(INIReader *config UNUSED) { return true; }

    virtual void sighup_received() { }

    virtual void end_of_log(uint32_t packet_count UNUSED,
                            uint64_t bytes_dropped UNUSED) { }

protected:
    virtual void idle_tenthHz() { }
    virtual void idle_1Hz() { }
    virtual void idle_10Hz() { }
    virtual void idle_100Hz() { }

};


#endif
