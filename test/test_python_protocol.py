import socket
import sys
import threading
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "python"))

from gawm import Gawm


class _PayloadReadBarrier:
    """Release a line only after the server has queued the reply ACK."""

    TIMEOUT = 2.0

    def __init__(self, final_payload_readline, payload_read, ack_sent):
        self._remaining = final_payload_readline
        self._payload_read = payload_read
        self._ack_sent = ack_sent
        self._triggered = False

    def release(self, line):
        if self._triggered:
            return line

        self._remaining -= 1
        if self._remaining == 0:
            self._triggered = True
            self._payload_read.set()
            if not self._ack_sent.wait(self.TIMEOUT):
                raise TimeoutError("fake server did not send the first reply ACK")
        return line


class _BarrierReader:
    def __init__(self, reader, barrier):
        self._reader = reader
        self._barrier = barrier

    def readline(self, *args, **kwargs):
        line = self._reader.readline(*args, **kwargs)
        return self._barrier.release(line)

    def __iter__(self):
        return self

    def __next__(self):
        return self._barrier.release(next(self._reader))

    def __getattr__(self, name):
        return getattr(self._reader, name)


class _TrackingSocket:
    """Delegate to a real socket and wrap only its first makefile reader."""

    def __init__(self, sock, barrier):
        self._sock = sock
        self._barrier = barrier
        self._reader_wrapped = False

    def makefile(self, *args, **kwargs):
        reader = self._sock.makefile(*args, **kwargs)
        if self._reader_wrapped:
            return reader
        self._reader_wrapped = True
        return _BarrierReader(reader, self._barrier)

    def __getattr__(self, name):
        return getattr(self._sock, name)


class _DelayedAckServer:
    """Queue reply one ACK before allowing its final payload line to return."""

    TIMEOUT = 2.0

    def __init__(
        self,
        first_command,
        first_payload,
        first_payload_readlines,
        second_command,
        second_payload,
    ):
        raw_client, self._server = socket.socketpair()
        raw_client.settimeout(self.TIMEOUT)
        self._server.settimeout(self.TIMEOUT)
        self._payload_read = threading.Event()
        self._ack_sent = threading.Event()
        barrier = _PayloadReadBarrier(
            first_payload_readlines, self._payload_read, self._ack_sent
        )
        self.client = _TrackingSocket(raw_client, barrier)
        self._first_command = first_command
        self._first_payload = first_payload
        self._second_command = second_command
        self._second_payload = second_payload
        self._done = threading.Event()
        self._error = None
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    @staticmethod
    def _expect_command(requests, expected):
        observed = requests.readline()
        if observed != expected + b"\n":
            raise AssertionError(
                f"expected command {expected!r}, observed {observed!r}"
            )

    def _run(self):
        try:
            with self._server, self._server.makefile("rb") as requests:
                self._expect_command(requests, self._first_command)
                self._server.sendall(self._first_payload)

                if not self._payload_read.wait(self.TIMEOUT):
                    raise AssertionError("client did not read the final payload line")
                self._server.sendall(b"\n")
                self._ack_sent.set()

                self._expect_command(requests, self._second_command)
                self._server.sendall(self._second_payload)
                self._server.sendall(b"\n")
        except BaseException as error:
            self._error = error
        finally:
            self._ack_sent.set()
            self._done.set()

    def finish(self):
        if not self._done.wait(self.TIMEOUT):
            raise AssertionError("fake server thread did not finish")
        self._thread.join()
        if self._error is not None:
            raise AssertionError(f"fake server failed: {self._error!r}") from self._error

    def close(self):
        self._payload_read.set()
        self._ack_sent.set()
        self.client.close()
        self._server.close()
        self._thread.join(self.TIMEOUT)


class ProtocolFramingTests(unittest.TestCase):
    def _client_for(self, server):
        self.addCleanup(server.close)
        client = Gawm()
        client.sock = server.client
        self.addCleanup(client.close)
        return client

    def test_delayed_ack_does_not_desynchronize_sequential_queries(self):
        server = _DelayedAckServer(
            b"get_data V(out)",
            b"1\n0.0 1.0\n",
            2,
            b"table_list",
            b"1\ntran\n",
        )
        client = self._client_for(server)

        data = client.get_data("V(out)")
        tables = client.get_tables()
        server.finish()

        self.assertIsNotNone(data)
        self.assertEqual([0.0], data["x"].tolist())
        self.assertEqual([1.0], data["y"].tolist())
        self.assertEqual(["tran"], tables)

    def test_delayed_ack_does_not_break_get_cursor_values(self):
        server = _DelayedAckServer(
            b"get_cursor 0",
            b"2.5\n",
            1,
            b"get_values_at 2.5",
            b"1\nV(out) 3.0\n",
        )
        client = self._client_for(server)

        result = client.get_cursor_values(0)
        server.finish()

        self.assertEqual({"x": 2.5, "values": {"V(out)": 3.0}}, result)
