import socket
import sys
import threading
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "python"))

from gawm import Gawm


class _PlotAckServer:
    """Model gawio's one blank success ACK for every received line."""

    TIMEOUT = 2.0
    UPLOAD_LINES = (
        "table_new python",
        "variables x wave",
        "rowdatas",
        "0.0 2.0",
        "1.0 3.0",
        "enddata",
    )
    QUERY_LINE = "table_list"

    def __init__(self):
        self.client, self._server = socket.socketpair()
        self.client.settimeout(self.TIMEOUT)
        self._server.settimeout(self.TIMEOUT)
        self.transcript = []
        self.upload_ack_count = 0
        self._done = threading.Event()
        self._error = None
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _expect_line(self, requests, expected):
        observed = requests.readline()
        if observed == b"":
            raise AssertionError(f"client closed before sending {expected!r}")
        self.transcript.append(observed.removesuffix(b"\n").decode("utf-8"))
        if observed != (expected + "\n").encode("utf-8"):
            raise AssertionError(
                f"expected protocol line {expected!r}, observed {observed!r}"
            )

    def _run(self):
        try:
            with self._server, self._server.makefile("rb") as requests:
                for line in self.UPLOAD_LINES:
                    self._expect_line(requests, line)
                    self._server.sendall(b"\n")
                    self.upload_ack_count += 1

                self._expect_line(requests, self.QUERY_LINE)
                self._server.sendall(b"1\npython\n\n")
        except BaseException as error:
            self._error = error
        finally:
            self._done.set()

    def finish(self):
        if not self._done.wait(self.TIMEOUT):
            raise AssertionError("fake server thread did not finish")
        self._thread.join(self.TIMEOUT)
        if self._thread.is_alive():
            raise AssertionError("fake server thread did not terminate")
        if self._error is not None:
            raise AssertionError(f"fake server failed: {self._error!r}") from self._error

    def close(self):
        for sock in (self.client, self._server):
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
        self._done.wait(self.TIMEOUT)
        self._thread.join(self.TIMEOUT)


class PlotProtocolFramingTests(unittest.TestCase):
    def test_plot_consumes_all_acks_before_next_query(self):
        server = _PlotAckServer()
        self.addCleanup(server.close)
        client = Gawm()
        client.sock = server.client
        self.addCleanup(client.close)

        client.plot([0.0, 1.0], [[2.0, 3.0]], ["wave"])
        tables = client.get_tables()
        server.finish()

        self.assertEqual(
            [*server.UPLOAD_LINES, server.QUERY_LINE], server.transcript
        )
        self.assertEqual(len(server.UPLOAD_LINES), server.upload_ack_count)
        self.assertEqual(["python"], tables)


if __name__ == "__main__":
    unittest.main()
