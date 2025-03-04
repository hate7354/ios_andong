#include "pch.h"
#include "GroupChannelInfo.h"

GroupChannelInfo::GroupChannelInfo() : _frame(0)
{
}

GroupChannelInfo::~GroupChannelInfo()
{
	_ch_sequences.clear();
}

void GroupChannelInfo::addChannel(int channel_seq)
{
	_ch_sequences.push_back(channel_seq);
}

void GroupChannelInfo::delChannel(int channel_seq)
{
	for (int i = 0; i < _ch_sequences.size(); i++) {
		if (_ch_sequences[i] == channel_seq) {
			_ch_sequences.erase(_ch_sequences.begin() + i);
			break;
		}
	}
}

bool GroupChannelInfo::FindChannel(int channel_seq) const
{
	bool bFound = false;

	for (int i = 0; i < _ch_sequences.size(); i++) {
		if (_ch_sequences[i] == channel_seq) {
			bFound = true;
			break;
		}
	}

	return bFound;
}

int GroupChannelInfo::getCount() const
{
	return _ch_sequences.size();
}

int GroupChannelInfo::getFrame() const
{
	
	return _frame;
}

void GroupChannelInfo::UpdateFrame()
{
	int num = _ch_sequences.size();

#if 0
	if (num <= 0)
		_frame = 0;
	else if (num <= 3)
		_frame = 6;
	else if (num <= 5)
		_frame = 4;
	else
		_frame = 2;
#else
	if (num <= 0)
		_frame = 0;
	else if (num <= 4)
		_frame = 2;
	else
		_frame = 1;
#endif	
}

std::vector<int> GroupChannelInfo::GetChannels() const
{
	return _ch_sequences;
}