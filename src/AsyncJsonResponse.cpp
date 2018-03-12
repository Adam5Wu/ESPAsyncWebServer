
#include "AsyncJsonResponse.h"

class BufferWindowPrint : public Print {
	private:
		uint8_t* _buf;
		size_t _win_start;
		size_t _win_size;
		size_t _prn_pos;
	public:
		BufferWindowPrint(uint8_t* buf, size_t len, size_t offset)
			: _buf(buf), _win_start(offset), _win_size(len), _prn_pos{0}
			{}

		virtual size_t write(uint8_t c) override {
			if (_win_start) {
				_win_start--;
				return 1;
			}
			if (_win_size) {
				_buf[_prn_pos++] = c;
				_win_size--;
				return 1;
			}
			return 0;
		}

		virtual size_t write(const uint8_t *buffer, size_t size) override {
			if (_win_start >= size) {
				_win_start -= size;
				return size;
			}
			size_t bufstart = _win_start;
			_win_start = 0;
			size_t writelen = min(size - bufstart, _win_size);
			if (writelen) {
				memcpy(_buf+_prn_pos, buffer+bufstart, writelen);
				_prn_pos+= writelen;
				_win_size-= writelen;
				return writelen;
			}
			return 0;
		}

		size_t printed_length() { return _prn_pos; }
};

size_t AsyncJsonResponse::_JsonFiller(uint8_t* buf, size_t len, size_t offset) {
	BufferWindowPrint WinBuf(buf, len, offset);
	if (_prettyPrint) _jsonRoot.prettyPrintTo(WinBuf);
	else _jsonRoot.printTo(WinBuf);
	ESPWS_DEBUGVV("[%s] Json buffer fill @%d, len %d, got %d\n",
		_request->_remoteIdent.c_str(), offset, len, WinBuf.printed_length());
	return WinBuf.printed_length();
}

void AsyncJsonResponse::setPrettyPrint(bool enable) {
	if (_started()) {
		ESPWS_LOG("[%s] ERROR: Response already started, cannot change pretty print!\n");
		return;
	}
	_prettyPrint = enable;
}
