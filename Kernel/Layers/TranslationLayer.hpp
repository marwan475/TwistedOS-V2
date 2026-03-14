#pragma once

class LogicLayer;

class TranslationLayer
{
	private:
		LogicLayer* Logic;

	public:
		TranslationLayer();
		void Initialize(LogicLayer* Logic);

		LogicLayer* GetLogicLayer() const;
};