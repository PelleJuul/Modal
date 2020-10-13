#include "plugin.hpp"
#include <stdlib.h>
#include <cmath>

#define pow2(x) (x * x)
#define MODAL_OVERSAMPLING (2)
#define NUM_MODES (20)

struct TonerigBigKnob : app::SvgKnob {
	TonerigBigKnob() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;
		std::string path = asset::plugin(pluginInstance, "res/TonerigBigKnob.svg");
		setSvg(APP->window->loadSvg(path));
	}
};

struct TonerigMediumKnob : app::SvgKnob {
	TonerigMediumKnob() {
		minAngle = -0.75 * M_PI;
		maxAngle = 0.75 * M_PI;
		std::string path = asset::plugin(pluginInstance, "res/TonerigMediumKnob.svg");
		setSvg(APP->window->loadSvg(path));
	}
};

/// Class representing a single modal resonator
class Modal
{
    public:
    float u = 0;	//	The current state.
    float up = 0;	// 	The previous state.
	float lp = 0;	// 	Low-pass filtering state (?)
    float m = 2000;	// 	Inverse modalmass
    float omega0 = 2 * M_PI * 440;	// Stiffness
    float sigma0 = 2.0;				// Damping

    float k = 1.0 / 44100;	// Sample period

	/// Set the sample rate
	void setSampleRate(float fs)
	{
		k = 1.0 / (MODAL_OVERSAMPLING * fs);
	}

	/// Get the next sample given with a force input.
	/// @param 	f	input force.
	/// @returns	the computed audio.
    float getNextSample(float f)
    {
		for (int i = 0; i < MODAL_OVERSAMPLING; i++)
		{
			float un = (1.0 / (1 + k * sigma0)) * (-pow2(k) * pow2(omega0) * u + sigma0 * k * up + 2.0 * u - up + pow2(k) * m * f);
			up = u;
			u = un;
		}

		lp = 0.9999 * lp + 0.0001 * u;

        return (1 + sigma0) * (u - lp);
    }
};

struct Tonerig_modal : Module {
	enum ParamIds {
		FREQ_PARAM,
		INHARM_PARAM,
		DAMP_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		VOCT_INPUT,
		INHARM_INPUT,
		DAMP_INPUT,
		IN_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	Modal modes[NUM_MODES];
	float inharmonicities[NUM_MODES];

	Tonerig_modal() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(INHARM_PARAM, -1.f, 1.f, 0.f, "");
		configParam(FREQ_PARAM, -48.f, 48.f, 0.f, "Frequency", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		configParam(DAMP_PARAM, 0.f, 10.f, 0.5f, "Damp");

		// Random initialize the inharmonicities.
		srand(42);
		inharmonicities[0] = 0;

		for (int i = 1; i < NUM_MODES; i++)
		{
			inharmonicities[i] = 1 - 2 * (rand() / (float)RAND_MAX);
		}
	}

	/// Generic linear mapping function.
	float map(float x, float oldMin, float oldMax, float newMin, float newMax)
	{
		float r = (x - oldMin) / (oldMax - oldMin);
		return newMin + r * (newMax - newMin);
	}

	/// Convert midi pitch to frequency.
	float pitchToFreq(float pitch)
	{
		return 440 * std::pow(2, (pitch - 69) / 12);
	}

	/// Constrain a value to a certain range.
	float constrain(float x, float min, float max)
	{
		if (x < min)
		{
			return min;
		}
		
		if (x > max)
		{
			return max;
		}

		return x;
	}

	/// Process audio.
	void process(const ProcessArgs& args) override {
		float y = 0;

		// Get audio input.
		float x = inputs[IN_INPUT].getVoltage();

		// Get knob positions.
		float pitch = params[FREQ_PARAM].getValue();
		float damp = params[DAMP_PARAM].getValue();
		float inharm = params[INHARM_PARAM].getValue();

		// Get CV inputs and apply.
		pitch += inputs[VOCT_INPUT].getVoltage() / 12.0;
		damp += map(inputs[DAMP_INPUT].getVoltage() / 5.0, -1, 1, -10, 10);;
		inharm += inputs[INHARM_INPUT].getVoltage() / 5.0;

		pitch = constrain(pitch, -48, 48);
		damp = constrain(damp, 0, 10);
		inharm = constrain(inharm, 0, 1);

		// Comput fundamental freq.
		float freq = pitchToFreq(60 + pitch);
		float K = 7643.022;

		// Compute how much damping should increase for each mode (overtone).
		float dampDec = map(damp, 5, 10.0, 1, 1.1);
		dampDec = fmax(dampDec, 1);
		
		// Get audio output for each mode.
		for (int i = 0; i < NUM_MODES; i++)
		{
			Modal &m = modes[i];

			// Compute the frequency for this mode.
			float f = (1 + inharm * inharmonicities[i]) * (i + 1) * freq;

			if (f >= 20000)
			{
				m.m = 0.0;
				break;
			}

			// Set parameters
			m.setSampleRate(2 * args.sampleRate);
			m.omega0 = 2 * M_PI * f;
			m.sigma0 = damp;
			m.m = pow2(m.omega0) / K;
			damp *= dampDec;

			// Compute sample
			y += m.getNextSample(x);
		}

		// Write output voltage.
		outputs[OUT_OUTPUT].setVoltage(5.0 * y);
	}
};


struct Tonerig_modalWidget : ModuleWidget {
	Tonerig_modalWidget(Tonerig_modal* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/tonerig-modal-illustrator.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<TonerigBigKnob>(mm2px(Vec(10.16, 20.32)), module, Tonerig_modal::FREQ_PARAM));
		addParam(createParam<TonerigMediumKnob>(mm2px(Vec(5.08, 50.8)), module, Tonerig_modal::INHARM_PARAM));
		addParam(createParam<TonerigMediumKnob>(mm2px(Vec(25.4, 50.8)), module, Tonerig_modal::DAMP_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.32, 73.66)), module, Tonerig_modal::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 91.44)), module, Tonerig_modal::INHARM_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.94, 91.44)), module, Tonerig_modal::DAMP_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 109.22)), module, Tonerig_modal::IN_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(27.94, 109.22)), module, Tonerig_modal::OUT_OUTPUT));
	}
};


Model* modelTonerig_modal = createModel<Tonerig_modal, Tonerig_modalWidget>("tonerig-modal");