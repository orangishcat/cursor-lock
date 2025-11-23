#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/loader/Setting.hpp>

#include <memory>

#include "CursorLock.hpp"

using namespace geode::prelude;
using namespace cursorlock;

namespace {
PercentBounds loadBoundsFromSettings() {
	auto mod = Mod::get();
	PercentBounds b{};
	b.left = mod->getSettingValue<double>("left");
	b.top = mod->getSettingValue<double>("top");
	b.right = mod->getSettingValue<double>("right");
	b.bottom = mod->getSettingValue<double>("bottom");
	return b;
}

PercentBounds defaultBounds() {
	return PercentBounds{25.0, 25.0, 75.0, 75.0};
}

bool isValidBounds(PercentBounds const& b) {
	return b.right > b.left && b.bottom > b.top;
}

class CursorLockManager : public CCNode {
public:
	static CursorLockManager* get() {
		static CursorLockManager* s_instance = nullptr;
		if (!s_instance) {
			s_instance = new CursorLockManager();
			if (s_instance && s_instance->init()) {
				s_instance->retain();
				CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(s_instance, 0, false);
			}
		}
		return s_instance;
	}

	bool init() override {
		if (!CCNode::init()) {
			return false;
		}
		m_api = createCursorLockAPI();
		m_bounds = clampBounds(loadBoundsFromSettings());
		if (!isValidBounds(m_bounds)) {
			m_bounds = clampBounds(defaultBounds());
		}
		m_lastValid = m_bounds;
		return true;
	}

	void update(float) override {
		if (m_enabled && m_api) {
			applyToAPI(); // re-apply in case the platform clears the clip when pausing/resuming
			m_api->tick();
		}
	}

	void refreshFromSettings() {
		setBounds(loadBoundsFromSettings());
	}

	void setBounds(PercentBounds bounds) {
		auto clamped = clampBounds(bounds);
		if (!isValidBounds(clamped)) {
			notifyInvalid(clamped);
			restoreLastValid();
			return;
		}
		m_bounds = clamped;
		m_lastValid = clamped;
		m_alertShown = false;
		if (m_enabled) {
			applyToAPI();
		}
	}

	[[nodiscard]] PercentBounds getBounds() const {
		return m_bounds;
	}

	void activate() {
		refreshFromSettings();
		m_enabled = true;
		applyToAPI();
	}

	void deactivate() {
		m_enabled = false;
		if (m_api) {
			m_api->release();
		}
	}

	[[nodiscard]] bool isActive() const {
		return m_enabled;
	}

	[[nodiscard]] bool isIgnoringChanges() const {
		return m_ignoreSettingChanges;
	}

private:
	void writeSettings(PercentBounds const& b) {
		m_ignoreSettingChanges = true;
		auto mod = Mod::get();
		mod->setSettingValue<double>("left", b.left);
		mod->setSettingValue<double>("top", b.top);
		mod->setSettingValue<double>("right", b.right);
		mod->setSettingValue<double>("bottom", b.bottom);
		m_ignoreSettingChanges = false;
	}

	void applyToAPI() {
		if (!m_api) {
			return;
		}
		m_api->applyBounds(percentToPixels(m_bounds));
	}

	void notifyInvalid(PercentBounds const&) {
		if (m_alertShown) {
			return;
		}
		FLAlertLayer::create("Cursor Lock", "Bounding box is invalid: right/bottom must be greater than left/top. Reverting to last valid values.", "OK")->show();
		m_alertShown = true;
	}

	void restoreLastValid() {
		if (!isValidBounds(m_lastValid)) {
			m_lastValid = clampBounds(defaultBounds());
		}
		m_bounds = m_lastValid;
		writeSettings(m_lastValid);
	}

	std::unique_ptr<CursorLockAPI> m_api;
	PercentBounds m_bounds{};
	PercentBounds m_lastValid{};
	bool m_enabled = false;
	bool m_alertShown = false;
	bool m_ignoreSettingChanges = false;
};
} // namespace

class $modify(MyMenuLayer, MenuLayer) {
	bool init() override {
		if (!MenuLayer::init()) {
			return false;
		}

		// Ensure manager exists so bounds are loaded.
		CursorLockManager::get();
		return true;
	}
};

class $modify(MyPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) override {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
			return false;
		}

		if (auto manager = CursorLockManager::get()) {
			manager->activate();
		}
		return true;
	}

	void onExit() override {
		if (auto manager = CursorLockManager::get()) {
			manager->deactivate();
		}
		PlayLayer::onExit();
	}
};

class $modify(MyPauseLayer, PauseLayer) {
	void onResume(CCObject* sender) override {
		if (auto manager = CursorLockManager::get()) {
			manager->activate();
		}
		PauseLayer::onResume(sender);
	}
};

$execute {
	auto mod = Mod::get();
	auto subscribe = [&](std::string const& key) {
		listenForSettingChanges<double>(key, [](double) {
			if (auto manager = CursorLockManager::get()) {
				if (manager->isIgnoringChanges()) return;
				manager->setBounds(loadBoundsFromSettings());
			}
		}, mod);
	};
	subscribe("left");
	subscribe("top");
	subscribe("right");
	subscribe("bottom");
}
