#include "editor.hpp"
#include "workspace_browser.hpp"
#include "dune3d_appwindow.hpp"
#include "widgets/constraints_box.hpp"
#include "document/group/all_groups.hpp"
#include "document/entity/entity_workplane.hpp"
#include "util/selection_util.hpp"
#include "canvas/canvas.hpp"
#include "workspace_browser.hpp"
#include "util/template_util.hpp"
#include "core/tool_id.hpp"
#include <iostream>

namespace dune3d {

void Editor::init_workspace_browser()
{
    m_workspace_browser = Gtk::make_managed<WorkspaceBrowser>(m_core);
    m_workspace_browser->signal_close_document().connect(
            [this](const UUID &doc_uu) { close_document(doc_uu, nullptr, nullptr); });
    m_workspace_browser->update_documents(m_document_view);

    m_workspace_browser->signal_group_selected().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_group_selected));
    m_workspace_browser->signal_add_group().connect(sigc::mem_fun(*this, &Editor::on_add_group));
    m_workspace_browser->signal_delete_current_group().connect(sigc::mem_fun(*this, &Editor::on_delete_current_group));
    m_workspace_browser->signal_move_group().connect(sigc::mem_fun(*this, &Editor::on_move_group));
    m_workspace_browser->signal_group_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_group_checked));
    m_workspace_browser->signal_body_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_body_checked));
    m_workspace_browser->signal_body_solid_model_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_body_solid_model_checked));


    m_workspace_browser->set_sensitive(m_core.has_documents());


    m_core.signal_rebuilt().connect([this] {
        Glib::signal_idle().connect_once([this] { m_workspace_browser->update_documents(m_document_view); });
    });

    m_win.get_left_bar().set_start_child(*m_workspace_browser);
}

void Editor::on_workspace_browser_group_selected(const UUID &uu_doc, const UUID &uu_group)
{
    if (m_core.tool_is_active())
        return;
    if (m_core.get_current_idocument_info().get_uuid() == uu_doc) {
        if (m_core.get_current_group() == uu_group)
            return;
        m_core.set_current_group(uu_group);
        m_workspace_browser->update_current_group(m_document_view);
        canvas_update_keep_selection();
        update_workplane_label();
        m_constraints_box->update();
        update_group_editor();
        update_action_sensitivity();
        update_action_bar_buttons_sensitivity();
        update_selection_editor();
    }
}

void Editor::on_add_group(Group::Type group_type)
{
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_current_document();
    auto &current_group = doc.get_group(m_core.get_current_group());
    Group *new_group = nullptr;
    if (group_type == Group::Type::SKETCH) {
        auto &group = doc.insert_group<GroupSketch>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_name = "Sketch";
    }
    else if (group_type == Group::Type::EXTRUDE) {
        if (!current_group.m_active_wrkpl)
            return;
        auto &group = doc.insert_group<GroupExtrude>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_name = "Extrude";
        group.m_wrkpl = current_group.m_active_wrkpl;
        group.m_dvec = doc.get_entity<EntityWorkplane>(group.m_wrkpl).get_normal_vector();
        group.m_source_group = current_group.m_uuid;
    }
    else if (group_type == Group::Type::LATHE) {
        if (!current_group.m_active_wrkpl)
            return;
        auto sel = get_canvas().get_selection();
        auto axis_enp = entity_and_point_from_selection(doc, sel);
        if (!axis_enp)
            return;
        if (axis_enp->point != 0)
            return;
        const auto &axis = doc.get_entity(axis_enp->entity);
        if (axis.get_type() != Entity::Type::WORKPLANE && axis.get_type() != Entity::Type::LINE_2D
            && axis.get_type() != Entity::Type::LINE_3D)
            return;

        auto &group = doc.insert_group<GroupLathe>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_name = "Lathe";
        group.m_wrkpl = current_group.m_active_wrkpl;
        group.m_source_group = current_group.m_uuid;
        group.m_origin = axis_enp->entity;
        group.m_origin_point = 1;
        group.m_normal = axis_enp->entity;
    }
    else if (group_type == Group::Type::FILLET) {
        auto &group = doc.insert_group<GroupFillet>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_name = "Fillet";
    }
    else if (group_type == Group::Type::CHAMFER) {
        auto &group = doc.insert_group<GroupChamfer>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_name = "Chamfer";
        m_core.set_needs_save();
    }
    else if (group_type == Group::Type::LINEAR_ARRAY) {
        auto &group = doc.insert_group<GroupLinearArray>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_name = "Linear array";
        group.m_active_wrkpl = current_group.m_active_wrkpl;
        group.m_source_group = current_group.m_uuid;
        m_core.set_needs_save();
    }
    else if (group_type == Group::Type::POLAR_ARRAY) {
        UUID wrkpl;
        if (current_group.m_active_wrkpl) {
            wrkpl = current_group.m_active_wrkpl;
        }
        else {
            auto sel = get_canvas().get_selection();
            auto owrkpl = entity_and_point_from_selection(doc, sel, Entity::Type::WORKPLANE);
            if (owrkpl)
                wrkpl = owrkpl->entity;
        }
        if (!wrkpl)
            return;
        auto &group = doc.insert_group<GroupPolarArray>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_name = "Polar array";
        group.m_active_wrkpl = wrkpl;
        group.m_source_group = current_group.m_uuid;
        m_core.set_needs_save();
    }
    if (new_group) {
        doc.set_group_generate_pending(new_group->m_uuid);
        m_core.set_needs_save();
        m_core.rebuild("add group");
        m_workspace_browser->update_documents(m_document_view);
        canvas_update_keep_selection();
        m_workspace_browser->select_group(new_group->m_uuid);
        if (any_of(group_type, Group::Type::FILLET, Group::Type::CHAMFER)) {
            trigger_action(ToolID::SELECT_EDGES);
        }
    }
}

void Editor::on_delete_current_group()
{
    if (m_core.tool_is_active())
        return;

    auto &doc = m_core.get_current_document();

    auto &group = doc.get_group(m_core.get_current_group());
    if (group.get_type() == Group::Type::REFERENCE)
        return;

    UUID previous_group;
    previous_group = doc.get_group_rel(group.m_uuid, -1);
    if (!previous_group)
        previous_group = doc.get_group_rel(group.m_uuid, 1);

    if (!previous_group)
        return;
    doc.set_group_generate_pending(previous_group);

    {
        ItemsToDelete items;
        items.groups = {group.m_uuid};
        const auto items_initial = items;
        auto extra_items = doc.get_additional_items_to_delete(items);
        items.append(extra_items);
        show_delete_items_popup(items_initial, items);
        doc.delete_items(items);
    }

    m_core.set_current_group(previous_group);

    m_core.set_needs_save();
    m_core.rebuild("delete group");
    canvas_update_keep_selection();
    m_workspace_browser->update_documents(m_document_view);
}

void Editor::on_move_group(Document::MoveGroup op)
{
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_current_document();
    auto group = m_core.get_current_group();

    UUID group_after = doc.get_group_after(group, op);
    if (!group_after)
        return;

    if (!doc.reorder_group(group, group_after))
        return;
    m_core.set_needs_save();
    m_core.rebuild("reorder_group");
    canvas_update_keep_selection();
    m_workspace_browser->update_documents(m_document_view);
}

void Editor::on_workspace_browser_group_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    m_document_view.m_group_views[uu_group].m_visible = checked;
    m_workspace_browser->update_current_group(m_document_view);
    canvas_update_keep_selection();
}

void Editor::on_workspace_browser_body_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    m_document_view.m_body_views[uu_group].m_visible = checked;
    m_workspace_browser->update_current_group(m_document_view);
    canvas_update_keep_selection();
}

void Editor::on_workspace_browser_body_solid_model_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{

    m_document_view.m_body_views[uu_group].m_solid_model_visible = checked;
    m_workspace_browser->update_current_group(m_document_view);
    canvas_update_keep_selection();
}

} // namespace dune3d
