"""Configure synthetic PTYs before taking an exact restoration snapshot."""


def terminal_baseline(fd):
    import termios
    # Solaris may expose initial ispeed=0 (use ospeed). Even an unchanged
    # tcsetattr materializes that rate in c_cflag/ispeed. Configure the fixture
    # first, then capture the actual state; never normalize the post-operation
    # result or omit any flags, control characters or speeds from comparison.
    termios.tcsetattr(fd, termios.TCSANOW, termios.tcgetattr(fd))
    return termios.tcgetattr(fd)


def assert_timeout_cleanup(fd, before, elapsed):
    import termios
    assert elapsed < 4, ('timeout cleanup too slow', elapsed)
    after = termios.tcgetattr(fd)
    assert after == before, ('terminal attributes changed', before, after)
