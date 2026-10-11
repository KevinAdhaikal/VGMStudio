#include "edit_song_dialog.hpp"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace vgmstudio {

EditSongDialog::EditSongDialog() {
}

EditSongDialog::~EditSongDialog() {
}

void EditSongDialog::Open(const VgmInfo& currentInfo) {
    m_isOpen = true;
    m_openRequested = true;

    auto safeCopy = [](char* dst, size_t dstSz, const std::string& src) {
        std::memset(dst, 0, dstSz);
        std::strncpy(dst, src.c_str(), dstSz - 1);
    };

    safeCopy(m_title, sizeof(m_title), currentInfo.title);
    safeCopy(m_titleJp, sizeof(m_titleJp), currentInfo.titleJp);
    safeCopy(m_game, sizeof(m_game), currentInfo.game);
    safeCopy(m_gameJp, sizeof(m_gameJp), currentInfo.gameJp);
    safeCopy(m_system, sizeof(m_system), currentInfo.system);
    safeCopy(m_systemJp, sizeof(m_systemJp), currentInfo.systemJp);
    safeCopy(m_author, sizeof(m_author), currentInfo.author);
    safeCopy(m_authorJp, sizeof(m_authorJp), currentInfo.authorJp);
    safeCopy(m_date, sizeof(m_date), currentInfo.date);
    safeCopy(m_encodedBy, sizeof(m_encodedBy), currentInfo.encodedBy);
    safeCopy(m_comment, sizeof(m_comment), currentInfo.comment);
}

bool EditSongDialog::Render(AudioEngine& audioEngine) {
    if (!m_isOpen) return false;

    if (m_openRequested) {
        ImGui::OpenPopup("Edit Song Info##Dialog");
        m_openRequested = false;
    }

    bool saved = false;

    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::BeginPopupModal("Edit Song Info##Dialog", &m_isOpen, flags)) {
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Edit Song Metadata");
        ImGui::TextDisabled("Edit GD3 metadata tags for the current song.");
        ImGui::Separator();
        ImGui::Spacing();

        float footerH = 48.0f;
        float availH = ImGui::GetContentRegionAvail().y;
        float contentH = std::max(60.0f, availH - footerH);

        ImGui::BeginChild("EditSongFieldsScroll", ImVec2(0.0f, contentH), false);

        float labelW = 100.0f;
        auto drawField = [&](const char* label, char* buffer, size_t bufSz) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s:", label);
            ImGui::SameLine(labelW);
            std::string id = "##" + std::string(label);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText(id.c_str(), buffer, bufSz);
        };

        drawField("Title", m_title, sizeof(m_title));
        drawField("Title (JP)", m_titleJp, sizeof(m_titleJp));
        ImGui::Spacing();

        drawField("Game", m_game, sizeof(m_game));
        drawField("Game (JP)", m_gameJp, sizeof(m_gameJp));
        ImGui::Spacing();

        drawField("System", m_system, sizeof(m_system));
        drawField("System (JP)", m_systemJp, sizeof(m_systemJp));
        ImGui::Spacing();

        drawField("Author", m_author, sizeof(m_author));
        drawField("Author (JP)", m_authorJp, sizeof(m_authorJp));
        ImGui::Spacing();

        drawField("Date", m_date, sizeof(m_date));
        drawField("Ripper", m_encodedBy, sizeof(m_encodedBy));
        ImGui::Spacing();

        ImGui::TextDisabled("Notes:");
        ImGui::SameLine(labelW);
        ImGui::InputTextMultiline("##Comment", m_comment, sizeof(m_comment), ImVec2(-1.0f, 65.0f));

        ImGui::EndChild();

        // Locked Footer Action Buttons
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 5));
        float saveW = ImGui::CalcTextSize("Save Changes").x + 32.0f;
        float cancelW = ImGui::CalcTextSize("Cancel").x + 32.0f;
        float totalButtonsW = saveW + cancelW + 10.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - totalButtonsW - 16.0f);

        if (ImGui::Button("Save Changes", ImVec2(saveW, 0))) {
            VgmInfo info = audioEngine.GetCurrentInfo();
            info.title = m_title;
            info.titleJp = m_titleJp;
            info.game = m_game;
            info.gameJp = m_gameJp;
            info.system = m_system;
            info.systemJp = m_systemJp;
            info.author = m_author;
            info.authorJp = m_authorJp;
            info.date = m_date;
            info.encodedBy = m_encodedBy;
            info.comment = m_comment;

            audioEngine.UpdateSongInfo(info);
            m_isOpen = false;
            saved = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(cancelW, 0))) {
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();

        ImGui::EndPopup();
    }

    return saved;
}

} // namespace vgmstudio
