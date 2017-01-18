import subprocess

import pytest
import time


@pytest.yield_fixture(scope='session', autouse=True)
def memcached_standard_port():
    p = subprocess.Popen(['../../build/memcacher', "-t1"], stdout=subprocess.PIPE, stderr=subprocess.PIPE);
    #fl = open('testlog.txt', 'w');
    #p = subprocess.Popen(['../../build/memcacher', "-t1"], stdout=fl, stderr=fl);
    time.sleep(0.1)
    yield p
    p.kill()
    p.wait()

