import unittest
import bmemcached
from bmemcached.compat import long, unicode

import six
if six.PY3:
    from unittest import mock
else:
    import mock


class MemcachedTests(unittest.TestCase):
    def setUp(self):
        self.server = '127.0.0.1:11211'
        self.client = None

    def tearDown(self):
        self.client.disconnect_all()

    def testSetGet(self):
        self.client = bmemcached.Client(self.server, 'user', 'password',
                                        socket_timeout=None)
        self.assertTrue(self.client.set('test_key', 'test test test test'))
        self.assertEqual(self.client.get('test_key'), 'test test test test')

        for x in range(10):
            self.assertTrue(self.client.set('test_key'+str(x), 'test test test test'+str(x)))
            self.assertEqual(self.client.get('test_key'+str(x)), 'test test test test'+str(x))

    def testCas(self):
        self.client = bmemcached.Client(self.server, 'user', 'password',
                                        socket_timeout=None)

        cas = 345
        self.assertTrue(self.client.cas('test_key1', 'test1', cas))
        self.assertEqual(self.client.get('test_key1'), 'test1')

        # same cas
        self.assertTrue(self.client.cas('test_key1', 'test2', cas))
        self.assertEqual(self.client.get('test_key1'), 'test2')

        # new cas
        cas = 567
        self.assertFalse(self.client.cas('test_key1', 'test3', cas))
        # still equal to old value
        self.assertEqual(self.client.get('test_key1'), 'test2')

