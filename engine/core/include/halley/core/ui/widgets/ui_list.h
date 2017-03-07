#pragma once

#include "../ui_widget.h"
#include "halley/core/graphics/sprite/sprite.h"
#include "halley/core/graphics/text/text_renderer.h"
#include "ui_button.h"

namespace Halley {
	class UIStyle;
	class UIListItem;

	class UIList : public UIWidget {
		friend class UIListItem;

	public:
		explicit UIList(const String& id, std::shared_ptr<UIStyle> style);

		void setSelectedOption(int option);
		int getSelectedOption() const;

		void addTextItem(const String& id, const String& label);
		void addItem(const String& id, std::shared_ptr<UIWidget> widget);

	protected:
		void draw(UIPainter& painter) const override;
		void update(Time t, bool moved) override;

	private:
		std::shared_ptr<UIStyle> style;
		Sprite sprite;

		int curOptionHighlight = -1;
		int curOption = 0;

		void onItemClicked(UIListItem& item);
	};

	class UIListItem : public UIClickable {
	public:
		explicit UIListItem(const String& id, UIList& parent, std::shared_ptr<UIStyle> style, std::shared_ptr<UIWidget> widget);

		void onClicked(Vector2f mousePos) override;
	
	protected:
		void draw(UIPainter& painter) const override;
		void update(Time t, bool moved) override;

	private:
		UIList& parent;
		std::shared_ptr<UIStyle> style;
		Sprite sprite;

		void doSetState(State state) override;
	};
}
