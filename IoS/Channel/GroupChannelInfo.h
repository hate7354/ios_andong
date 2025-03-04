#pragma once

#include <vector>
#include <string>

class GroupChannelInfo {
private:
	std::vector<int> _ch_sequences;
	int _frame = 0;

public:
	GroupChannelInfo();
	~GroupChannelInfo();
	void addChannel(int channel_seq);
	void delChannel(int channel_seq);
	bool FindChannel(int channel_seq) const;
	int getCount() const;
	void UpdateFrame();
	int getFrame() const;
	std::vector<int> GetChannels() const;
};
