// UIPanel.h

#ifndef UIPANEL_H
#define UIPANEL_H

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>



namespace Engine::UI
{
    /**
     * @brief The logical layer the UI panel belongs to.
     *
     * Used for grouped rendering: editor panels, runtime panels, debug panels, etc.
     */
    enum class UILayerKind { Editor, Runtime, Debug };

    /**
     * @brief Description of a single UI panel registered in the UIPanelRegistry.
     *
     * Instances of this structure are added to the registry and managed by it.
     */
    struct PanelEntry {
        // id — a unique identifier of the panel. It is used as a key in UIPanelRegistry::panels to quickly find the desired panel.
        // It must be unique for each registered panel, otherwise, register_panel will return false.
        std::string id;

        // draw — the pane's render function. A lambda or function is passed here that defines how the panel should look and what to draw.
        // It is called inside draw_layer() if the panel is visible and belongs to the correct layer. If draw is nullptr, the panel will simply not be rendered.
        std::function<void()> draw;

        // visible — a flag indicating whether the panel should be shown. It is controlled by the methods set_visible, toggle, show_only, and show_by_tags.
        // During rendering in draw_layer(), if visible == false, the panel is skipped. This can be used to temporarily hide a panel without removing it from the registry.
        bool visible = true;

        // order — the rendering order of the panel. The smaller the number, the earlier the panel will be drawn within its layer.
        // In draw_layer(), all panels are sorted by this field before calling their draw function. Useful when order matters — for example, tool panels should appear before the status panel.
        int order = 0;

        // layer — the layer to which the panel belongs. Panels are divided into layers such as Editor, Runtime, and Debug — this helps group them by purpose.
        // In the draw_layer(layer) method, only panels belonging to the specified layer are rendered. For example, you can render all editor panels separately from runtime panels.
        UILayerKind layer = UILayerKind::Editor;

        // tags — a list of tags describing the panel. They are used by the show_by_tags() method: if at least one of the panel's tags matches the requested tags, the panel becomes visible.
        // This is useful when you need to enable or disable panels in groups (for example, all panels with the 'physics' or 'tools' tag). Tags can also be used for other filtering or logic in the future.
        std::vector<std::string> tags;
    };

    /**
     * @brief UI panel registry: registration, visibility management, and layered rendering.
     *
     * The class stores panels by their id key and provides convenient methods
     * for bulk visibility control and ordered rendering.
     *
     * @note Not thread-safe. If used from multiple threads, external synchronization is required.
     */
    class UIPanelRegistry {
    public:
        /**
         * @brief Registers a new panel in the registry.
         *
         * @param e: A fully initialized PanelEntry. The id field must be unique.
         * @return true if the panel was successfully added; false if a panel with the same id already exists.
         */
        bool register_panel(PanelEntry e) {
            auto it = panels.find(e.id);
            if (it != panels.end()) return false;
            panels.emplace(e.id, std::move(e));
            return true;
        }



        /**
         * @brief Sets the visibility of a panel by its identifier.
         *
         * @param id: The panel identifier.
         * @param v: The new visibility value (true — show, false — hide).
         * @return true if the panel was found and the flag was changed; false if no panel with the given id exists.
         */
        bool set_visible(const std::string& id, bool v) {
            auto it = panels.find(id);
            if (it == panels.end()) return false;
            it->second.visible = v;
            return true;
        }



        /**
         * @brief Toggles the visibility of a panel.
         *
         * @param id: The panel identifier.
         * @return true if the panel was found and its visibility was successfully toggled; false otherwise.
         */
        bool toggle(const std::string& id) {
            auto it = panels.find(id);
            if (it == panels.end()) return false;
            it->second.visible = !it->second.visible;
            return true;
        }



        /**
         * @brief Makes only the specified panels visible and hides all others.
         *
         * @param ids: A list of panel identifiers that should remain visible.
         * @return (nothing)
         */
        void show_only(const std::vector<std::string>& ids) {
            std::unordered_set<std::string> want(ids.begin(), ids.end());
            for (auto& [id, p] : panels) p.visible = want.count(id) != 0;
        }



        /**
         * @brief Enables visibility of panels based on matching tags.
         *
         * A panel becomes visible if it has at least one tag from @p wanted_tags.
         * Panels without any matching tags are hidden.
         *
         * @param wanted_tags: The set of tags used for filtering.
         * @return (nothing)
         */
        void show_by_tags(const std::vector<std::string>& wanted_tags) {
            std::unordered_set<std::string> w(wanted_tags.begin(), wanted_tags.end());
            for (auto& [id, p] : panels) {
                bool match = false;
                for (auto& t : p.tags) if (w.count(t)) { match = true; break; }
                p.visible = match;
            }
        }



        /**
         * @brief Renders all visible panels of the specified layer in ascending order of order.
         * 
         * The method collects pointers to matching panels, sorts them by PanelEntry::order,
         * and calls their PanelEntry::draw() functions if defined.
         *
         * @param layer: The target layer whose panels should be rendered.
         * @return (nothing)
         */
        void draw_layer(UILayerKind layer) {
            collect_tmp.clear();
            collect_tmp.reserve(panels.size());
            for (auto& [id, p] : panels) if (p.visible && p.layer == layer) collect_tmp.push_back(&p);
            std::sort(collect_tmp.begin(), collect_tmp.end(),
                [](const PanelEntry* a, const PanelEntry* b) { return a->order < b->order; });
            for (auto* p : collect_tmp) if (p->draw) p->draw();
        }



        /**
         * @brief Returns a sorted list of all registered panel identifiers.
         *
         * @return A vector of string id values sorted in ascending order.
         */
        std::vector<std::string> list_ids() const {
            std::vector<std::string> ids; ids.reserve(panels.size());
            for (auto& [id, _] : panels) ids.push_back(id);
            std::sort(ids.begin(), ids.end());
            return ids;
        }



    private:
        /**
         * @brief Storage of panels by their unique identifier.
         * The key is PanelEntry::id, and the value is the full panel description.
         */
        std::unordered_map<std::string, PanelEntry> panels;

        /**
         * @brief A temporary buffer collection of panel pointers used in draw_layer().
         *
         * It is populated on each call to draw_layer() to avoid allocations during sorting or iteration.
         */
        std::vector<PanelEntry*> collect_tmp;
    };
}


#endif // UIPANEL_H